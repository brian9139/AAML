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
#include "tensorflow/lite/kernels/internal/common.h"
#include "tensorflow/lite/kernels/internal/portable_tensor_utils.h"

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
  for (int batch = 0; batch < batches; ++batch) {

    // int stop=0;

    for (int out_y = 0; out_y < output_height; ++out_y) {
      const int in_y_origin = (out_y * stride_height) - pad_height;
      for (int out_x = 0; out_x < output_width; ++out_x) {
        const int in_x_origin = (out_x * stride_width) - pad_width;
        for (int out_channel = 0; out_channel < output_depth; ++out_channel) {
          auto group = out_channel / filters_per_group;

          int32_t acc = cfu_op0( 1, 0, 0);
          
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
              int in_channel = 0;
              while (in_channel < filter_input_depth) {
                 if(in_channel+4 <= filter_input_depth){ 
                    uint32_t input_val = *((uint32_t *)(input_data + Offset(
                                          input_shape, batch, in_y, in_x, in_channel + group * filter_input_depth)));
                    uint32_t filter_val = *((uint32_t *)(filter_data + Offset(
                    filter_shape, out_channel, filter_y, filter_x, in_channel)));
                    acc = cfu_op0( 0, input_val, filter_val);
                    in_channel+=4; 
                }
                else{
                    int32_t input_val =(input_data[Offset(input_shape, batch, in_y, in_x,
                                        in_channel + group * filter_input_depth)]+input_offset);
                    int32_t filter_val = filter_data[Offset(filter_shape, out_channel, filter_y, filter_x, in_channel)];
                    acc += filter_val * input_val;
                    in_channel++;
                }
              
              // for (int in_channel = 0; in_channel < input_depth; in_channel += 4) {

                // Extract four input values and add the quantization offset
                // int32_t input_val3 = (input_data[Offset(input_shape, batch, in_y, in_x,
                //                       in_channel + group * filter_input_depth)]+128);
                // int32_t input_val2 = (input_data[Offset(input_shape, batch, in_y, in_x,
                //                       in_channel + 1 + group * filter_input_depth)]+128);
                // int32_t input_val1 = (input_data[Offset(input_shape, batch, in_y, in_x,
                //                       in_channel + 2 + group * filter_input_depth)]+128);
                // int32_t input_val0 = (input_data[Offset(input_shape, batch, in_y, in_x,
                //                       in_channel + 3 + group * filter_input_depth)]+128);
                // int8_t ori_3 = (input_data[Offset(input_shape, batch, in_y, in_x,
                //                       in_channel + group * filter_input_depth)]);
                // int8_t ori_2 = (input_data[Offset(input_shape, batch, in_y, in_x,
                //                       in_channel +1+ group * filter_input_depth)]);
                // int8_t ori_1 = (input_data[Offset(input_shape, batch, in_y, in_x,
                //                       in_channel +2+ group * filter_input_depth)]);
                // int8_t ori_0 = (input_data[Offset(input_shape, batch, in_y, in_x,
                //                       in_channel +3+ group * filter_input_depth)]);

                // // Extract four filter values
                // int8_t ori_filter = filter_data[Offset(filter_shape, out_channel, filter_y, filter_x, in_channel)];

                // int32_t filter_val3 = filter_data[Offset(filter_shape, out_channel, filter_y, filter_x, in_channel)];
                // int32_t filter_val2 = filter_data[Offset(filter_shape, out_channel, filter_y, filter_x, in_channel + 1)];
                // int32_t filter_val1 = filter_data[Offset(filter_shape, out_channel, filter_y, filter_x, in_channel + 2)];
                // int32_t filter_val0 = filter_data[Offset(filter_shape, out_channel, filter_y, filter_x, in_channel + 3)];


                // Pack four input values into a 32-bit word
                // uint32_t input_packed = (input_val0 |
                //                          ((input_val1)<< 8) |
                //                          ((input_val2) << 16) |
                                        //  ((input_val3) << 24));
                // uint32_t input_packed = ((static_cast<uint32_t>(input_val3) ) |
                //                          ((static_cast<uint32_t>(input_val2) ) << 8) |
                //                          ((static_cast<uint32_t>(input_val1) ) << 16) |
                //                          ((static_cast<uint32_t>(input_val0) ) << 24));


                // Pack four filter values into a 32-bit word
                // uint32_t filter_packed = ((static_cast<uint32_t>(filter_val0) & 0xFF) |
                //                           ((static_cast<uint32_t>(filter_val1) & 0xFF) << 8) |
                //                           ((static_cast<uint32_t>(filter_val2) & 0xFF) << 16) |
                //                           ((static_cast<uint32_t>(filter_val3) & 0xFF) << 24));
// if(stop==0){
//   printf("ori_3: 0x%02x\n:",ori_3);
//   printf("ori_2: 0x%02x\n:",ori_2);
//   printf("ori_1: 0x%02x\n:",ori_1);
//   printf("ori_0: 0x%02x\n:",ori_0);
                                   
//   printf("input_val3: 0x%08lx\n", input_val3);
//   printf("input_val2: 0x%08lx\n", input_val2);
//   printf("input_val1: 0x%08lx\n", input_val1);
  // printf("input_val: 0x%08lx\n", input_val);
//   printf("                 \n");
  // printf("ori: 0x%02x\n:",ori_filter);
  // printf("filter_val3: 0x%08lx\n", filter_val3);
  // printf("filter_val2: 0x%08lx\n", filter_val2);
  // printf("filter_val1: 0x%08lx\n", filter_val1);
  // printf("filter_val: 0x%08lx\n", filter_val);
  // printf("                 \n");
  // printf("input_packed_val: 0x%08lx\n", input_packed);
  // printf("filter_packed_val: 0x%08lx\n",filter_packed);

  // stop++;
// }
                // Call CFU to perform four multiply-accumulate operations
                // Assuming funct3 and funct7 are set appropriately inside cfu_op0
                // acc += cfu_op0(0, input_packed + total_offset, filter_packed);
              }
            }
          }

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
  }
}

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
