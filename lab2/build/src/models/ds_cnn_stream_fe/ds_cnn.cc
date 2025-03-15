#include "models/ds_cnn_stream_fe/ds_cnn.h"
#include <stdio.h>
#include "menu.h"
#include "models/ds_cnn_stream_fe/ds_cnn_stream_fe.h"
#include "tflite.h"
#include "models/label/label0_board.h"
#include "models/label/label1_board.h"
#include "models/label/label6_board.h"
#include "models/label/label8_board.h"
#include "models/label/label11_board.h"

// Initialize everything once
// deallocate tensors when done
static void ds_cnn_stream_fe_init(void) {
  tflite_load_model(ds_cnn_stream_fe, ds_cnn_stream_fe_len);
}

// Helper function to run inference and print output scores
static void ds_cnn_stream_fe_classify(const float* input_data) {
  tflite_set_input(input_data);   // Set the input data
  tflite_classify();              // Run inference

  // Get output scores 
  int32_t* output = (int32_t*)tflite_get_output(); 
  printf("Output Scores (in hex):\n");
  for (int i = 0; i < 12; i++) {
    printf("Score %d: 0x%08lx,\n", i, (unsigned long)output[i]); // Print as 32-bit hex
  }
}
// Example function to classify with label0_board data
static void classify_with_label0() {
  puts("Classifying with label0_board data");
  ds_cnn_stream_fe_classify(label0_data);
}

// Example function to classify with label1_board data
static void classify_with_label1() {
  puts("Classifying with label1_board data");
  ds_cnn_stream_fe_classify(label1_data);
}

// Add more functions for other labels (label6, label8, label11)
static void classify_with_label6() {
  puts("Classifying with label6_board data");
  ds_cnn_stream_fe_classify(label6_data);
}

static void classify_with_label8() {
  puts("Classifying with label8_board data");
  ds_cnn_stream_fe_classify(label8_data);
}

static void classify_with_label11() {
  puts("Classifying with label11_board data");
  ds_cnn_stream_fe_classify(label11_data);
}

// Menu structure for running classification tests
static struct Menu MENU = {
    "Tests for ds_cnn_stream_fe",
    "ds_cnn_stream_fe",
    {
        MENU_ITEM('1', "Run with zeros input", classify_with_label0),
        MENU_ITEM('2', "Classify with label0", classify_with_label0),
        MENU_ITEM('3', "Classify with label1", classify_with_label1),
        MENU_ITEM('4', "Classify with label6", classify_with_label6),
        MENU_ITEM('5', "Classify with label8", classify_with_label8),
        MENU_ITEM('6', "Classify with label11", classify_with_label11),
        MENU_END,
    },
};

// For integration into menu system
void ds_cnn_stream_fe_menu() {
  ds_cnn_stream_fe_init();  // Initialize and load model
  menu_run(&MENU);          // Run the menu for user interaction
}
