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
                       ::tflite::ops::builtin::Register_QUANTIZE());
  resolver->AddBuiltin(::tflite::BuiltinOperator_SPACE_TO_DEPTH,
                       ::tflite::ops::builtin::Register_SPACE_TO_DEPTH());
  resolver->AddBuiltin(::tflite::BuiltinOperator_SPLIT,
                       ::tflite::ops::builtin::Register_SPLIT());
  resolver->AddBuiltin(::tflite::BuiltinOperator_CONCATENATION,
                       ::tflite::ops::builtin::Register_CONCATENATION());
  resolver->AddBuiltin(::tflite::BuiltinOperator_DEPTH_TO_SPACE,
                       ::tflite::ops::builtin::Register_DEPTH_TO_SPACE());
  resolver->AddBuiltin(::tflite::BuiltinOperator_FILL,
                       ::tflite::ops::builtin::Register_FILL());
  resolver->AddBuiltin(::tflite::BuiltinOperator_LOGISTIC,
                       ::tflite::ops::builtin::Register_LOGISTIC());
  resolver->AddBuiltin(::tflite::BuiltinOperator_MEAN,
                       ::tflite::ops::builtin::Register_MEAN());
  resolver->AddBuiltin(::tflite::BuiltinOperator_MUL,
                       ::tflite::ops::builtin::Register_MUL());
  resolver->AddBuiltin(::tflite::BuiltinOperator_PACK,
                       ::tflite::ops::builtin::Register_PACK());
  resolver->AddBuiltin(::tflite::BuiltinOperator_REDUCE_MAX,
                       ::tflite::ops::builtin::Register_REDUCE_MAX());
  resolver->AddBuiltin(::tflite::BuiltinOperator_RESHAPE,
                       ::tflite::ops::builtin::Register_RESHAPE());
  resolver->AddBuiltin(::tflite::BuiltinOperator_RSQRT,
                       ::tflite::ops::builtin::Register_RSQRT());
  resolver->AddBuiltin(::tflite::BuiltinOperator_SHAPE,
                       ::tflite::ops::builtin::Register_SHAPE());
  resolver->AddBuiltin(::tflite::BuiltinOperator_SQUARED_DIFFERENCE,
                       ::tflite::ops::builtin::Register_SQUARED_DIFFERENCE());
  resolver->AddBuiltin(::tflite::BuiltinOperator_STRIDED_SLICE,
                       ::tflite::ops::builtin::Register_STRIDED_SLICE());
  resolver->AddBuiltin(::tflite::BuiltinOperator_SUB,
                       ::tflite::ops::builtin::Register_SUB());
  resolver->AddBuiltin(::tflite::BuiltinOperator_TRANSPOSE,
                       ::tflite::ops::builtin::Register_TRANSPOSE(), 1, 4);
}
