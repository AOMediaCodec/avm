#include "av1/tflite_models/op_registrations.h"
#include "tensorflow/lite/kernels/builtin_op_kernels.h"

void RegisterSelectedOpsAllQps(::tflite::MutableOpResolver *resolver) {
  resolver->AddBuiltin(::tflite::BuiltinOperator_ADD,
                       ::tflite::ops::builtin::Register_ADD(), 1, 2);
  resolver->AddBuiltin(::tflite::BuiltinOperator_CONV_2D,
                       ::tflite::ops::builtin::Register_CONV_2D(), 1, 3);
  resolver->AddBuiltin(::tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
                       ::tflite::ops::builtin::Register_DEPTHWISE_CONV_2D(), 1,
                       3);
  resolver->AddBuiltin(::tflite::BuiltinOperator_DEQUANTIZE,
                       ::tflite::ops::builtin::Register_DEQUANTIZE(), 2, 2);
  resolver->AddBuiltin(::tflite::BuiltinOperator_MIRROR_PAD,
                       ::tflite::ops::builtin::Register_MIRROR_PAD());
  resolver->AddBuiltin(::tflite::BuiltinOperator_PAD,
                       ::tflite::ops::builtin::Register_PAD(), 1, 2);
  resolver->AddBuiltin(::tflite::BuiltinOperator_QUANTIZE,
                       ::tflite::ops::builtin::Register_QUANTIZE(), 1, 2);
  resolver->AddBuiltin(::tflite::BuiltinOperator_CONCATENATION,
                       ::tflite::ops::builtin::Register_CONCATENATION(), 1, 2);
  resolver->AddBuiltin(::tflite::BuiltinOperator_MAX_POOL_2D,
                       ::tflite::ops::builtin::Register_MAX_POOL_2D(), 1, 2);
  resolver->AddBuiltin(::tflite::BuiltinOperator_RESIZE_BILINEAR,
                       ::tflite::ops::builtin::Register_RESIZE_BILINEAR(), 3,
                       3);
  resolver->AddBuiltin(::tflite::BuiltinOperator_SHAPE,
                       ::tflite::ops::builtin::Register_SHAPE());
  resolver->AddBuiltin(::tflite::BuiltinOperator_SPLIT_V,
                       ::tflite::ops::builtin::Register_SPLIT_V(), 1, 2);
  resolver->AddBuiltin(::tflite::BuiltinOperator_STRIDED_SLICE,
                       ::tflite::ops::builtin::Register_STRIDED_SLICE(), 1, 1);
  resolver->AddBuiltin(::tflite::BuiltinOperator_PACK,
                       ::tflite::ops::builtin::Register_PACK(), 1, 1);
  resolver->AddBuiltin(::tflite::BuiltinOperator_TRANSPOSE_CONV,
                       ::tflite::ops::builtin::Register_TRANSPOSE_CONV(), 4, 4);
}
