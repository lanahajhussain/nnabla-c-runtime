// Copyright (c) 2017 Sony Corporation. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <nnablart/functions.h>

#include <assert.h>

#include "../../../utilities.h"

struct affine_impl {
  affine_config_t config;

  rt_variable_t *input;
  rt_variable_getter get_input;

  rt_variable_t *weight;
  rt_variable_getter get_weight;

  rt_variable_t *bias;
  rt_variable_getter get_bias;

  rt_variable_t *output;
  int output_size;
  rt_variable_setter set_output;
  rt_variable_getter get_output;

  int base_loop_size;
  int input_loop_size;
  int output_loop_size;
};
typedef struct affine_impl affine_impl_t;

static void exec_affine_float(rt_function_t *f);
static void exec_affine_generic(rt_function_t *f);

// Affine
void allocate_affine_config(rt_function_t *f) {
  WHOAMI("%s\n", __func__);

  assert(f->num_of_inputs == 2 || f->num_of_inputs == 3);
  assert(f->num_of_outputs == 1);
  void *buf = realloc(f->config, sizeof(affine_impl_t));
  if (!buf) {
    buf = malloc(sizeof(affine_impl_t));
    memcpy(buf, f->config, sizeof(affine_config_t));
    f->config = buf;
  }
  affine_impl_t *const pimpl = buf;
  affine_config_t *config = pimpl->config;

  pimpl->input = f->inputs[0];
  pimpl->get_input = select_getter(pimpl->input);

  pimpl->weight = f->inputs[1];
  pimpl->get_weight = select_getter(pimpl->weight);

  pimpl->output = f->outputs[0];
  pimpl->get_output = select_getter(pimpl->output);
  pimpl->set_output = select_setter(pimpl->output);

  if (f->num_of_inputs > 2) {
    pimpl->bias = f->inputs[2];
    pimpl->get_bias = select_getter(pimpl->bias);
  } else {
    pimpl->bias = 0;
  }

  pimpl->output_size = calc_shape_size(pimpl->output->shape);

  int base_axis = config->base_axis;
  int i; // Iterator

  pimpl->base_loop_size = 1;
  for (i = 0; i < base_axis; i++) {
    pimpl->base_loop_size *= pimpl->input->shape.data[i];
  }

  pimpl->input_loop_size = 1;
  for (i = base_axis; i < pimpl->input->shape.size; i++) {
    pimpl->input_loop_size *= pimpl->input->shape.data[i];
  }

  pimpl->output_loop_size = 1;
  for (i = base_axis; i < pimpl->output->shape.size; i++) {
    pimpl->output_loop_size *= pimpl->output->shape.data[i];
  }
}

void free_affine_config(rt_function_t *f) {
  realloc(f->config, sizeof(affine_config_t)); // can be omitted
}

void exec_affine(rt_function_t *f) {
  affine_impl_t *const pimpl = f->config;
  if (pimpl->input->type == NN_DATA_TYPE_FLOAT &&
      pimpl->output->type == NN_DATA_TYPE_FLOAT &&
      pimpl->weight->type == NN_DATA_TYPE_FLOAT &&
      (!pimpl->bias || pimpl->bias->type == NN_DATA_TYPE_FLOAT) {
    exec_affine_float(f);
  } else {
    exec_affine_generic(f);
  }
}

void exec_affine_float(rt_function_t *f) {
  WHOAMI("%s\n", __func__);

  affine_impl_t *const pimpl = f->config;

  int i, j, k; // Iterators.
  float *input = (float *)(pimpl->input->data);
  float *weight = (float *)(pimpl->weight->data);
  float *output = (float *)(pimpl->output->data);

  // Clear output
  memset(output, 0, sizeof(float) * pimpl->output_size);

  for (k = 0; k < pimpl->base_loop_size; k++) {
    int output_offset = k * pimpl->output_loop_size;
    int input_offset = k * pimpl->input_loop_size;

    // Weight
    for (j = 0; j < pimpl->input_loop_size; j++) {
      int ipos = input_offset + j;
      int weight_offset = j * pimpl->output_loop_size;

      float u = *(input + ipos);
      for (i = 0; i < pimpl->output_loop_size; i++) {
        int opos = output_offset + i;
        int wpos = weight_offset + i;

        float w = *(weight + wpos);
        float value = *(output + opos);
        *(output + opos) = value + u * w;
      }
    }

    // Bias
    if (pimpl->bias) {
      float *bias = (float *)(pimpl->bias->data);
      for (i = 0; i < pimpl->output_loop_size; i++) {
        int opos = output_offset + i;
        int bpos = i;
        *(output + opos) = *(output + opos) + *(bias + bpos);
      }
    }
  }
}

void exec_affine_generic(rt_function_t *f) {
  WHOAMI("%s\n", __func__);

  affine_impl_t *const pimpl = f->config;

  int i, j, k; // Iterators.

  // Clear output
  for (i = 0; i < pimpl->output_size; i++) {
    pimpl->set_output(pimpl->output, i, 0);
  }

  for (k = 0; k < pimpl->base_loop_size; k++) {
    int output_offset = k * pimpl->output_loop_size;
    int input_offset = k * pimpl->input_loop_size;

    // Weight
    for (j = 0; j < pimpl->input_loop_size; j++) {
      int ipos = input_offset + j;
      int weight_offset = j * pimpl->output_loop_size;

      float u = pimpl->get_input(pimpl->input, ipos);
      for (i = 0; i < pimpl->output_loop_size; i++) {
        int opos = output_offset + i;
        int wpos = weight_offset + i;

        float w = pimpl->get_weight(pimpl->weight, wpos);
        float value = pimpl->get_output(pimpl->output, opos);
        pimpl->set_output(pimpl->output, opos, value + u * w);
      }
    }

    // Bias
    if (pimpl->bias) {
      for (i = 0; i < pimpl->output_loop_size; i++) {
        int opos = output_offset + i;
        int bpos = i;
        pimpl->set_output(pimpl->output, opos, pimpl->get_output(pimpl->output, opos) +
                                           pimpl->get_bias(pimpl->bias, bpos));
      }
    }
  }
}
