/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#ifndef TENSORFLOW_LITE_KERNELS_INTERNAL_REFERENCE_INTEGER_OPS_CONV_H_
#define TENSORFLOW_LITE_KERNELS_INTERNAL_REFERENCE_INTEGER_OPS_CONV_H_

#include <algorithm>

#include "cfu.h"
#include "perf.h"

#include "tensorflow/lite/kernels/internal/common.h"
#include "tensorflow/lite/kernels/internal/portable_tensor_utils.h"
#include <cstdio>

int32_t matrix_input_fmaps[1024][1024];
int32_t matrix_filter[1024][1024];
int32_t matrix_result[1024][1024];

void matrix_multiply2D(int rowsA, int colsA, int rowsB, int colsB) {
    // Check if matrix dimensions are compatible for multiplication
    if (colsA != rowsB) {
        printf("Error: Incompatible matrix dimensions for multiplication\n");
        return;
    }

    // Perform matrix multiplication
    for (int i = 0; i < rowsA; ++i) {
        for (int j = 0; j < colsB; ++j) {
            matrix_result[i][j] = 0;
            for (int k = 0; k < colsA; ++k) {
                matrix_result[i][j] += matrix_input_fmaps[i][k] * matrix_filter[k][j];
            }
        }
    }
}

constexpr int T = 4;  // Tile size, adjust based on the hardware buffer size.

void tiled_matrix_multiply2D_SIMD(int rowsA, int colsA, int rowsB, int colsB, int input_offset = 0) {
    // Ensure dimensions are compatible
    if (colsA != rowsB) {
        printf("Error: Incompatible matrix dimensions for multiplication\n");
        return;
    }

    // Zero-initialize result matrix
    for (int i = 0; i < rowsA; ++i) {
        for (int j = 0; j < colsB; ++j) {
            matrix_result[i][j] = 0;
        }
    }

    // Set parameters K, M, N in hardware
    int M = rowsA;
    int K = colsA;
    int N = colsB;
    cfu_op0(1, 0, 0);      // Reset
    cfu_op0(2, K, K);      // Set parameter K
    cfu_op0(4, 4, M);      // Set parameter M
    cfu_op0(6, 4, N);      // Set parameter N

    // Tiling logic for fixed tile size 4xK and Kx4
    for (int m = 0; m < rowsA; m += T) {
        for (int n = 0; n < colsB; n += T) {
            int M_tile = std::min(T, rowsA - m);   // Size of M tile (4 or remaining rows)
            int N_tile = std::min(T, colsB - n);   // Size of N tile (4 or remaining cols)

            // Update hardware parameters for current tile
            cfu_op0(4, M_tile, M_tile);  // Update M
            cfu_op0(6, N_tile, N_tile);  // Update N

            // Send data to hardware buffers

            // Set A buffer (4xK)
            for (int i = 0; i < M_tile; ++i) {
                for (int t = 0; t < K; ++t) { // Full K dimension
                    int idx_i = m + i;
                    int idx_t = t;
                    int32_t a_val = matrix_input_fmaps[idx_i][idx_t] + input_offset;
                    cfu_op0(8, i * K + t, a_val);
                }
            }

            // Set B buffer (Kx4)
            for (int t = 0; t < K; ++t) { // Full K dimension
                for (int j = 0; j < N_tile; ++j) {
                    int idx_t = t;
                    int idx_j = n + j;
                    int32_t b_val = matrix_filter[idx_t][idx_j];
                    cfu_op0(10, t * N_tile + j, b_val);
                }
            }

            // Start computation
            cfu_op0(12, 0, 0);

            // Retrieve results
            for (int i = 0; i < M_tile; ++i) {
                for (int j = 0; j < N_tile; j += 4) {
                    int idx_i = m + i;
                    int idx_j = n + j;

                    // Read 4 results from hardware
                    matrix_result[idx_i][idx_j]     += cfu_op0(17, i * N_tile + j, 0);
                    if (j + 1 < N_tile) matrix_result[idx_i][idx_j + 1] += cfu_op0(16, i * N_tile + j + 1, 0);
                    if (j + 2 < N_tile) matrix_result[idx_i][idx_j + 2] += cfu_op0(15, i * N_tile + j + 2, 0);
                    if (j + 3 < N_tile) matrix_result[idx_i][idx_j + 3] += cfu_op0(14, i * N_tile + j + 3, 0);
                }
            }
        }
    }
}


namespace tflite {
namespace reference_integer_ops {

// Fixed-point per-channel-quantization convolution reference kernel.
inline void ConvPerChannel(
    const ConvParams& params, const int32_t* output_multiplier,
    const int32_t* output_shift, const RuntimeShape& input_shape,
    const int8_t* input_data, const RuntimeShape& filter_shape,
    const int8_t* filter_data, const RuntimeShape& bias_shape,
    const int32_t* bias_data, const RuntimeShape& output_shape,
    int8_t* output_data) {
    perf_enable_counter(6);
  // Get parameters.
  const int32_t input_offset = params.input_offset;  // r = s(q - Z)
  const int stride_width = params.stride_width;
  const int stride_height = params.stride_height;
  const int dilation_width_factor = params.dilation_width_factor;
  const int dilation_height_factor = params.dilation_height_factor;
  const int pad_width = params.padding_values.width;
  const int pad_height = params.padding_values.height;
  const int32_t output_offset = params.output_offset;

  // Set min and max value of the output.
  const int32_t output_activation_min = params.quantized_activation_min;
  const int32_t output_activation_max = params.quantized_activation_max;

  // Consistency check.
  TFLITE_DCHECK_LE(output_activation_min, output_activation_max);
  TFLITE_DCHECK_EQ(input_shape.DimensionsCount(), 4);
  TFLITE_DCHECK_EQ(filter_shape.DimensionsCount(), 4);
  TFLITE_DCHECK_EQ(output_shape.DimensionsCount(), 4);
  const int batches = MatchingDim(input_shape, 0, output_shape, 0);
  const int input_depth = input_shape.Dims(3);
  const int output_depth = MatchingDim(filter_shape, 0, output_shape, 3);
  if (bias_data) {
    TFLITE_DCHECK_EQ(bias_shape.FlatSize(), output_depth);
  }

  // Check dimensions of the tensors.
  const int input_height = input_shape.Dims(1);
  const int input_width = input_shape.Dims(2);
  const int filter_height = filter_shape.Dims(1);
  const int filter_width = filter_shape.Dims(2);
  const int filter_input_depth = filter_shape.Dims(3);
  const int groups = input_depth / filter_input_depth;
  TFLITE_DCHECK_EQ(input_depth % filter_input_depth, 0);
  const int filters_per_group = output_depth / groups;
  const int output_height = output_shape.Dims(1);
  const int output_width = output_shape.Dims(2);

  // input_fmaps_num * input_fmaps_size
  int input_fmaps_num = output_height * output_width;
  int input_fmaps_size = input_depth * filter_height * filter_width;

  // filter_size * filter_num
  int filter_size = input_fmaps_size;
  int filter_num = output_depth;

  // result_size * result_channel
  // int result_size = input_fmaps_num;
  // int result_num = filter_num;

  // printf("\n\n");
  // printf("input_fmaps_num: %d\n",input_fmaps_num);
  // printf("input_fmaps_size: %d\n",input_fmaps_size);
  // printf("filter_size: %d\n",filter_size);
  // printf("filter_num: %d\n",filter_num);
  // printf("result_size: %d\n", result_size);
  // printf("result_num: %d\n",result_num);
  int32_t acc=0;

  int input_fmaps_row;
  int input_fmaps_col;

  int filter_row;
  int filter_col;

  int result_row;
  int result_col;

  for (int batch = 0; batch < batches; ++batch)
    for (int out_y = 0; out_y < output_height; ++out_y)
      for (int out_x = 0; out_x < output_width; ++out_x)
        for (int out_channel = 0; out_channel < output_depth; ++out_channel) {
          output_data[Offset(output_shape, batch, out_y, out_x, out_channel)] =
		  static_cast<int8_t> (0);
	}


  for (int batch = 0; batch < batches; ++batch) {

      
//-------------------------------------------------------------------------------
    
//  input_fmaps prepare : 3D to 2D

    for (int out_y = 0; out_y < output_height; ++out_y) {
      const int in_y_origin = (out_y * stride_height) - pad_height;
      for (int out_x = 0; out_x < output_width; ++out_x) {
        const int in_x_origin = (out_x * stride_width) - pad_width;       
        for (int out_channel = 0; out_channel < output_depth; ++out_channel) {
          auto group = out_channel / filters_per_group;

          for (int filter_y = 0; filter_y < filter_height; ++filter_y) {
            const int in_y = in_y_origin + dilation_height_factor * filter_y;

            for (int filter_x = 0; filter_x < filter_width; ++filter_x) {
              const int in_x = in_x_origin + dilation_width_factor * filter_x;
        //       // Zero padding by omitting the areas outside the image.
        //       const bool is_point_inside_image = (in_x >= 0) && (in_x < input_width) && (in_y >= 0) && (in_y < input_height);

        //       if (!is_point_inside_image) {
        //         continue;
        //       }

	      for (int in_channel = 0; in_channel < filter_input_depth; ++in_channel) {

              input_fmaps_row = out_x + out_y * output_width; //input_fmaps_num // location index
              input_fmaps_col = filter_x + filter_y * filter_width  + in_channel * filter_height * filter_width; //input_fmaps_size //content * channel

              int32_t input_val = 0;
              if ((in_x >= 0) && (in_x < input_width) &&(in_y >= 0) && (in_y < input_height)) {
                input_val = input_data[Offset(input_shape, batch, in_y, in_x, in_channel + group * filter_input_depth)] + input_offset;
              }
              matrix_input_fmaps[input_fmaps_row][input_fmaps_col] = input_val;
             }
            }
          }
	}
      }
    }

//----------------------------------------------------------------------------------------------------
//  filter prepare : 4D to 2D

for (int out_channel = 0; out_channel < output_depth; ++out_channel) {
  for (int filter_y = 0; filter_y < filter_height; ++filter_y) {
    for (int filter_x = 0; filter_x < filter_width; ++filter_x) {
      for (int in_channel = 0; in_channel < filter_input_depth; ++in_channel) {
        int32_t filter_val = filter_data[Offset(filter_shape, out_channel, filter_y, filter_x, in_channel)];

        filter_row = filter_x + filter_y * filter_width + in_channel * filter_height * filter_width; // filter_size
        filter_col = out_channel; // filter_num

        //matrix_filter[filter_row * filter_num + filter_col] = filter_val;
        matrix_filter[filter_row][filter_col] = filter_val;
      }
    }
  }
}
//----------------------------------------------------------------------------------------------------

//  matrix_multiply(matrix_input_fmaps, input_fmaps_num, input_fmaps_size, matrix_filter, filter_size, filter_num, matrix_result);
  // matrix_multiply2D( input_fmaps_num, input_fmaps_size,  filter_size, filter_num);
 tiled_matrix_multiply2D_SIMD ( input_fmaps_num, input_fmaps_size,  filter_size, filter_num); 
//----------------------------------------------------------------------------------------------------


  for (int out_channel = 0; out_channel < output_depth; ++out_channel) {
    for (int out_y = 0; out_y < output_height; ++out_y) {
      for (int out_x = 0; out_x < output_width; ++out_x) {
        
          result_row = out_x + out_y * output_width; // result_size
          result_col = out_channel; // result_num

//	  acc = matrix_result[result_row * result_num + result_col];
	  acc = matrix_result[result_row][result_col];

	  if (bias_data) {
            acc += bias_data[out_channel];
          }
          acc = MultiplyByQuantizedMultiplier(
              acc, output_multiplier[out_channel], output_shift[out_channel]);
          acc += output_offset;
          acc = std::max(acc, output_activation_min);
          acc = std::min(acc, output_activation_max);
          output_data[Offset(output_shape, batch, out_y, out_x, out_channel)] =
              static_cast<int8_t>(acc);
        }
      }
    }
  } // batch
  perf_disable_counter(6);
}  // ConvPerChannel

inline void ConvPerChannelWithPackedInt4Weights(
    const ConvParams& params, const int32_t* output_multiplier,
    const int32_t* output_shift, const RuntimeShape& input_shape,
    const int8_t* input_data, const RuntimeShape& filter_shape,
    const int8_t* filter_input, int8_t* unpacked_filter_data,
    const RuntimeShape& bias_shape, const int32_t* bias_data,
    const RuntimeShape& output_shape, int8_t* output_data) {
  TFLITE_DCHECK(unpacked_filter_data != nullptr);
  tflite::tensor_utils::UnpackDenseInt4IntoInt8(
      filter_input, filter_shape.FlatSize(), unpacked_filter_data);
  ConvPerChannel(params, output_multiplier, output_shift, input_shape,
                 input_data, filter_shape, unpacked_filter_data, bias_shape,
                 bias_data, output_shape, output_data);
}

// Fixed-point per-channel-quantization convolution reference kernel.
// 16-bit data and 8-bit filter
template <typename AccumScalar>
inline void ConvPerChannel(
    const ConvParams& params, const int32_t* output_multiplier,
    const int32_t* output_shift, const RuntimeShape& input_shape,
    const int16_t* input_data, const RuntimeShape& filter_shape,
    const int8_t* filter_data, const RuntimeShape& bias_shape,
    const AccumScalar* bias_data, const RuntimeShape& output_shape,
    int16_t* output_data) {
  // Get parameters.
  const int stride_width = params.stride_width;
  const int stride_height = params.stride_height;
  const int dilation_width_factor = params.dilation_width_factor;
  const int dilation_height_factor = params.dilation_height_factor;
  const int pad_width = params.padding_values.width;
  const int pad_height = params.padding_values.height;

  // Set min and max value of the output.
  const int32_t output_activation_min = params.quantized_activation_min;
  const int32_t output_activation_max = params.quantized_activation_max;

  // Consistency check.
  TFLITE_DCHECK_LE(output_activation_min, output_activation_max);
  TFLITE_DCHECK_EQ(input_shape.DimensionsCount(), 4);
  TFLITE_DCHECK_EQ(filter_shape.DimensionsCount(), 4);
  TFLITE_DCHECK_EQ(output_shape.DimensionsCount(), 4);
  const int batches = MatchingDim(input_shape, 0, output_shape, 0);
  const int input_depth = input_shape.Dims(3);
  const int output_depth = MatchingDim(filter_shape, 0, output_shape, 3);
  if (bias_data) {
    TFLITE_DCHECK_EQ(bias_shape.FlatSize(), output_depth);
  }

  // Check dimensions of the tensors.
  const int input_height = input_shape.Dims(1);
  const int input_width = input_shape.Dims(2);
  const int filter_height = filter_shape.Dims(1);
  const int filter_width = filter_shape.Dims(2);
  const int filter_input_depth = filter_shape.Dims(3);
  const int groups = input_depth / filter_input_depth;
  TFLITE_DCHECK_EQ(input_depth % filter_input_depth, 0);
  const int filters_per_group = output_depth / groups;
  const int output_height = output_shape.Dims(1);
  const int output_width = output_shape.Dims(2);
  for (int batch = 0; batch < batches; ++batch) {
    for (int out_y = 0; out_y < output_height; ++out_y) {
      const int in_y_origin = (out_y * stride_height) - pad_height;
      for (int out_x = 0; out_x < output_width; ++out_x) {
        const int in_x_origin = (out_x * stride_width) - pad_width;
        for (int out_channel = 0; out_channel < output_depth; ++out_channel) {
          auto group = out_channel / filters_per_group;
          AccumScalar acc = 0;
          for (int filter_y = 0; filter_y < filter_height; ++filter_y) {
            const int in_y = in_y_origin + dilation_height_factor * filter_y;
            for (int filter_x = 0; filter_x < filter_width; ++filter_x) {
              const int in_x = in_x_origin + dilation_width_factor * filter_x;

              // Zero padding by omitting the areas outside the image.
              const bool is_point_inside_image =
                  (in_x >= 0) && (in_x < input_width) && (in_y >= 0) &&
                  (in_y < input_height);

              if (!is_point_inside_image) {
                continue;
              }

              for (int in_channel = 0; in_channel < filter_input_depth;
                   ++in_channel) {
                int32_t input_val =
                    input_data[Offset(input_shape, batch, in_y, in_x,
                                      in_channel + group * filter_input_depth)];
                int32_t filter_val = filter_data[Offset(
                    filter_shape, out_channel, filter_y, filter_x, in_channel)];
                // Accumulate with 64 bits accumulator.
                // int64_t += int8_t * int16_t so the highest value we can
                // get from each accumulation is [-127, 127] * ([-32768,
                // 32767] -
                // [-32768, 32767]), which is [-8322945, 8322945].
                // log2(8322945) = 22.99.
                acc += filter_val * input_val;
              }
            }
          }
          if (bias_data) {
            acc += bias_data[out_channel];
          }
          int32_t scaled_acc = MultiplyByQuantizedMultiplier(
              acc, output_multiplier[out_channel], output_shift[out_channel]);
          scaled_acc = std::max(scaled_acc, output_activation_min);
          scaled_acc = std::min(scaled_acc, output_activation_max);
          output_data[Offset(output_shape, batch, out_y, out_x, out_channel)] =
              static_cast<int16_t>(scaled_acc);
        }
      }
    }
  }
}

}  // namespace reference_integer_ops
}  // namespace tflite

#endif  // TENSORFLOW_LITE_KERNELS_INTERNAL_REFERENCE_INTEGER_OPS_CONV_H_