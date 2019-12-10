#include <ATen/ATen.h>
#include <ATen/NativeFunctions.h>
#include <ATen/core/op_registration/op_registration.h>
#include <ATen/native/TensorIterator.h>
#include <ATen/native/cpu/Loops.h>
#include <ATen/quantized/Quantizer.h>
#include <ATen/native/quantized/cpu/quantized_ops.h>
#include <ATen/native/quantized/cpu/init_qnnpack.h>
#include <ATen/native/quantized/cpu/qnnpack_utils.h>
#include <caffe2/utils/threadpool/ThreadPoolMobile.h>

#include <algorithm>

namespace at {
namespace native {

DEFINE_DISPATCH(qtanh_stub);

#ifdef USE_PYTORCH_QNNPACK
Tensor qnnpack_tanh(Tensor input) {
  Tensor qy;
  TORCH_CHECK(input.ndimension() > 0, "qnnpack_tanh(): Got empty input tensor");

  initQNNPACK();

  Tensor input_contig = input.contiguous();
  size_t num_elems_x = 1;
  for (int i = 1; i < input_contig.ndimension(); ++i) {
    num_elems_x *= input_contig.size(i);
  }

  const auto zero_point = input_contig.q_zero_point();
  const auto scale = input_contig.q_scale();

  pytorch_qnnp_operator_t tanh_op{nullptr};
  const pytorch_qnnp_status createStatus = pytorch_qnnp_create_tanh_nc_q8(
    num_elems_x /* channels */,
    zero_point /* input zero point */,
    scale /* input scale */,
    zero_point /* output zero point */,
    scale /* output scale */,
    zero_point /* output min */,
    std::numeric_limits<uint8_t>::max() /* output max */,
    0 /* flags */,
    &tanh_op);
  TORCH_INTERNAL_ASSERT(createStatus == pytorch_qnnp_status_success,
                        "failed to create QNNPACK TanH operator");
  qy = at::_empty_affine_quantized(
    input_contig.sizes(),
    input.options(),
    input_contig.q_scale(),
    input_contig.q_zero_point());
  size_t num_elems_y = input_contig.numel() / qy.size(0);

  const pytorch_qnnp_status setupStatus = pytorch_qnnp_setup_tanh_nc_q8(
    tanh_op,
    input_contig.size(0) /* batch size */,
    (uint8_t*)input_contig.data_ptr<c10::quint8>() /* input data */,
    num_elems_x /* input stride */,
    (uint8_t*)qy.data_ptr<c10::quint8>() /* output data */,
    num_elems_y /* output stride */);
  TORCH_INTERNAL_ASSERT(setupStatus == pytorch_qnnp_status_success,
                        "failed to setup QNNPACK TanH operator");

  pthreadpool_t threadpool = caffe2::mobile_pthreadpool();

  const pytorch_qnnp_status runStatus =
    pytorch_qnnp_run_operator(tanh_op, threadpool);

  TORCH_INTERNAL_ASSERT(
    runStatus == pytorch_qnnp_status_success,
    "failed to run QNNPACK TanH operator");
  return qy;
}
#endif

Tensor quantized_tanh(const Tensor& qx) {
  #ifdef USE_PYTORCH_QNNPACK
  if (at::globalContext().qEngine() == at::QEngine::QNNPACK && qx.scalar_type() == kQUInt8) {
    return qnnpack_tanh(qx);
  }
  #endif
  Tensor qy;
  qtanh_stub(qx.device().type(), qx, qy);
  return qy;
}
}}  // namespace at::native
