#include <unistd.h>
#include "device.hpp"
#include "host_tensor.hpp"
#include "transform_forward_convolution_into_gemm_v4r4r4_nhwc_kyxc_nhwk.hpp"
#include "driver_gemm_xdlops_v2r3.hpp"

#if 0
__host__ __device__ static constexpr auto
MakePaddedGridDescriptors(const AGridDesc_K0Raw_MRaw_K1& a_grid_desc_k0raw_mraw_k1,
                          const BGridDesc_K0Raw_NRaw_K1& b_grid_desc_k0raw_nraw_k1,
                          const CGridDesc_MRaw_NRaw& c_grid_desc_mraw_nraw)
{
    const auto K0Raw = a_grid_desc_k0raw_mraw_k1.GetLength(I0);
    const auto K1    = a_grid_desc_k0raw_mraw_k1.GetLength(I2);
    const auto MRaw  = c_grid_desc_mraw_nraw.GetLength(I0);
    const auto NRaw  = c_grid_desc_mraw_nraw.GetLength(I1);

    const auto K0Pad = math::integer_least_multiple(K0Raw, K0PerBlock) - K0Raw;
    const auto MPad  = math::integer_least_multiple(MRaw, MPerBlock) - MRaw;
    const auto NPad  = math::integer_least_multiple(NRaw, NPerBlock) - NRaw;

    // A
    const auto a_grid_desc_k0_m_k1 = [&]() {
        if constexpr(DoPad_K0 && DoPad_M)
        {
            return transform_tensor_descriptor(
                a_grid_desc_k0_m_k1,
                make_tuple(make_right_pad_transform(K0Raw, K0Pad),
                           make_right_pad_transform(MRaw, MPad),
                           make_pass_through_transform(K1)),
                make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}),
                make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}));
        }
        else if constexpr(DoPad_K0 && !DoPad_M)
        {
            return transform_tensor_descriptor(
                a_grid_desc_k0_m_k1,
                make_tuple(make_right_pad_transform(K0Raw, K0Pad),
                           make_pass_through_transform(MRaw),
                           make_pass_through_transform(K1)),
                make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}),
                make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}));
        }
        else if constexpr(!DoPad_K0 && DoPad_M)
        {
            return transform_tensor_descriptor(
                a_grid_desc_k0_m_k1,
                make_tuple(make_pass_through_transform(K0Raw),
                           make_right_pad_transform(MRaw, MPad),
                           make_pass_through_transform(K1)),
                make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}),
                make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}));
        }
        else
        {
            return a_grid_desc_k0raw_mraw_k1;
        }
    }();

    // B
    const auto b_grid_desc_k0_n_k1 = [&]() {
        if constexpr(DoPad_K0 && DoPad_N)
        {
            return transform_tensor_descriptor(
                b_grid_desc_k0_n_k1,
                make_tuple(make_right_pad_transform(K0Raw, K0Pad),
                           make_right_pad_transform(NRaw, NPad),
                           make_pass_through_transform(K1)),
                make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}),
                make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}));
        }
        else if constexpr(DoPad_K0 && !DoPad_N)
        {
            return transform_tensor_descriptor(
                b_grid_desc_k0_n_k1,
                make_tuple(make_right_pad_transform(K0Raw, K0Pad),
                           make_pass_through_transform(NRaw),
                           make_pass_through_transform(K1)),
                make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}),
                make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}));
        }
        else if constexpr(!DoPad_K0 && DoPad_N)
        {
            return transform_tensor_descriptor(
                b_grid_desc_k0_n_k1,
                make_tuple(make_pass_through_transform(K0Raw),
                           make_right_pad_transform(NRaw, NPad),
                           make_pass_through_transform(K1)),
                make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}),
                make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}));
        }
        else
        {
            return b_grid_desc_k0raw_nraw_k1;
        }
    }();

    // C
    const auto c_grid_desc_m_n = [&]() {
        if constexpr(DoPad_M && DoPad_N)
        {
            return transform_tensor_descriptor(c_grid_desc_m_n,
                                               make_tuple(make_right_pad_transform(MRaw, MPad),
                                                          make_right_pad_transform(NRaw, NPad)),
                                               make_tuple(Sequence<0>{}, Sequence<1>{}),
                                               make_tuple(Sequence<0>{}, Sequence<1>{}));
        }
        else if constexpr(DoPad_M && !DoPad_N)
        {
            return transform_tensor_descriptor(
                c_grid_desc_m_n,
                make_tuple(make_right_pad_transform(MRaw, MPad), make_pass_through_transform(NRaw)),
                make_tuple(Sequence<0>{}, Sequence<1>{}),
                make_tuple(Sequence<0>{}, Sequence<1>{}));
        }
        else if constexpr(!DoPad_M && DoPad_N)
        {
            return transform_tensor_descriptor(
                c_grid_desc_m_n,
                make_tuple(make_pass_through_transform(MRaw), make_right_pad_transform(NRaw, NPad)),
                make_tuple(Sequence<0>{}, Sequence<1>{}),
                make_tuple(Sequence<0>{}, Sequence<1>{}));
        }
        else
        {
            reutnr c_grid_desc_m_n;
        }
    }();
}
#endif

template <typename TInWei,
          typename TAcc,
          typename TOut,
          typename InLengths,
          typename WeiLengths,
          typename OutLengths,
          typename ConvStrides,
          typename ConvDilations,
          typename InLeftPads,
          typename InRightPads>
void device_convolution_forward_implicit_gemm_v4r4r4_xdlops_nhwc_kyxc_nhwk(
    const InLengths& in_n_hi_wi_c_lengths,
    const WeiLengths& wei_k_y_x_c_lengths,
    const OutLengths& out_n_ho_wo_k_lengths,
    const ConvStrides& conv_strides,
    const ConvDilations& conv_dilations,
    const InLeftPads& in_left_pads,
    const InRightPads& in_right_pads,
    const Tensor<TInWei>& in_n_hi_wi_c,
    const Tensor<TInWei>& wei_k_y_x_c,
    Tensor<TOut>& out_n_ho_wo_k,
    ck::index_t nrepeat)
{
    using namespace ck;

    std::cout << __func__ << std::endl;

    constexpr auto I0 = Number<0>{};
    constexpr auto I1 = Number<1>{};
    constexpr auto I2 = Number<2>{};
    constexpr auto I3 = Number<3>{};

    DeviceMem in_n_hi_wi_c_device_buf(sizeof(TInWei) * in_n_hi_wi_c.mDesc.GetElementSpace());
    DeviceMem wei_k_y_x_c_device_buf(sizeof(TInWei) * wei_k_y_x_c.mDesc.GetElementSpace());
    DeviceMem out_n_ho_wo_k_device_buf(sizeof(TOut) * out_n_ho_wo_k.mDesc.GetElementSpace());

    in_n_hi_wi_c_device_buf.ToDevice(in_n_hi_wi_c.mData.data());
    wei_k_y_x_c_device_buf.ToDevice(wei_k_y_x_c.mData.data());
    out_n_ho_wo_k_device_buf.ToDevice(out_n_ho_wo_k.mData.data());

    const auto in_n_hi_wi_c_desc  = make_naive_tensor_descriptor_packed(in_n_hi_wi_c_lengths);
    const auto wei_k_y_x_c_desc   = make_naive_tensor_descriptor_packed(wei_k_y_x_c_lengths);
    const auto out_n_ho_wo_k_desc = make_naive_tensor_descriptor_packed(out_n_ho_wo_k_lengths);

#if 0
    // [M, N, K0, K1] = [256, 128, 4, 4], C = 128, for fp32
    constexpr index_t BlockSize = 256;

    constexpr index_t GemmMPerBlock = 256;
    constexpr index_t GemmNPerBlock = 128;
    constexpr index_t GemmKPerBlock = 4;

    constexpr index_t GemmMPerXDL = 32;
    constexpr index_t GemmNPerXDL = 32;
    constexpr index_t GemmK1       = 4;

    constexpr index_t MRepeat = 4;
    constexpr index_t NRepeat = 2;

    using GemmABlockTransferThreadSliceLengths_GemmK0_GemmM_GemmK1   = Sequence<1, 4, 4>;
    using GemmABlockTransferThreadClusterLengths_GemmK0_GemmM_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmABlockTransferSrcScalarPerVector_GemmK1 = 4;
    constexpr index_t GemmABlockTransferDstScalarPerVector_GemmK1 = 4;

    using GemmBBlockTransferThreadSliceLengths_GemmK0_GemmN_GemmK1   = Sequence<1, 2, 4>;
    using GemmBBlockTransferThreadClusterLengths_GemmK0_GemmN_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmBBlockTransferSrcScalarPerVector_GemmK1 = 4;
    constexpr index_t GemmBBlockTransferDstScalarPerVector_GemmK1 = 4;

    constexpr index_t GemmCThreadTransferDstScalarPerVector = 1;
#elif 0
    // [M, N, K0, K1] = [128, 128, 4, 4], C = 128, for fp32
    constexpr index_t BlockSize = 256;

    constexpr index_t GemmMPerBlock = 128;
    constexpr index_t GemmNPerBlock = 128;
    constexpr index_t GemmKPerBlock = 4;

    constexpr index_t GemmMPerXDL = 32;
    constexpr index_t GemmNPerXDL = 32;
    constexpr index_t GemmK1      = 4;

    constexpr index_t MRepeat = 2;
    constexpr index_t NRepeat = 2;

    using GemmABlockTransferThreadSliceLengths_GemmK0_GemmM_GemmK1   = Sequence<1, 2, 4>;
    using GemmABlockTransferThreadClusterLengths_GemmK0_GemmM_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmABlockTransferSrcScalarPerVector_GemmK1 = 4;
    constexpr index_t GemmABlockTransferDstScalarPerVector_GemmK1 = 4;

    using GemmBBlockTransferThreadSliceLengths_GemmK0_GemmN_GemmK1   = Sequence<1, 2, 4>;
    using GemmBBlockTransferThreadClusterLengths_GemmK0_GemmN_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmBBlockTransferSrcScalarPerVector_GemmK1 = 4;
    constexpr index_t GemmBBlockTransferDstScalarPerVector_GemmK1 = 4;

    constexpr index_t GemmCThreadTransferDstScalarPerVector = 1;
#elif 0
    // [M, N, K0, K1] = [256, 256, 4, 8], C = 256, for fp16
    constexpr index_t BlockSize = 256;

    constexpr index_t GemmMPerBlock = 256;
    constexpr index_t GemmNPerBlock = 256;
    constexpr index_t GemmKPerBlock = 4;

    constexpr index_t GemmMPerXDL = 32;
    constexpr index_t GemmNPerXDL = 32;
    constexpr index_t GemmK1      = 8;

    constexpr index_t MRepeat = 4;
    constexpr index_t NRepeat = 4;

    using GemmABlockTransferThreadSliceLengths_GemmK0_GemmM_GemmK1   = Sequence<1, 4, 8>;
    using GemmABlockTransferThreadClusterLengths_GemmK0_GemmM_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmABlockTransferSrcScalarPerVector_GemmK1 = 8;
    constexpr index_t GemmABlockTransferDstScalarPerVector_GemmK1 = 8;

    using GemmBBlockTransferThreadSliceLengths_GemmK0_GemmN_GemmK1   = Sequence<1, 4, 8>;
    using GemmBBlockTransferThreadClusterLengths_GemmK0_GemmN_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmBBlockTransferSrcScalarPerVector_GemmK1 = 8;
    constexpr index_t GemmBBlockTransferDstScalarPerVector_GemmK1 = 8;

    constexpr index_t GemmCThreadTransferDstScalarPerVector = 1;
#elif 0
    // [M, N, K0, K1] = [256, 128, 4, 8], C = 128, for fp16
    constexpr index_t BlockSize = 256;

    constexpr index_t GemmMPerBlock = 256;
    constexpr index_t GemmNPerBlock = 128;
    constexpr index_t GemmKPerBlock = 4;

    constexpr index_t GemmMPerXDL = 32;
    constexpr index_t GemmNPerXDL = 32;
    constexpr index_t GemmK1      = 8;

    constexpr index_t MRepeat = 4;
    constexpr index_t NRepeat = 2;

    using GemmABlockTransferThreadSliceLengths_GemmK0_GemmM_GemmK1   = Sequence<1, 4, 8>;
    using GemmABlockTransferThreadClusterLengths_GemmK0_GemmM_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmABlockTransferSrcScalarPerVector_GemmK1 = 8;
    constexpr index_t GemmABlockTransferDstScalarPerVector_GemmK1 = 8;

    using GemmBBlockTransferThreadSliceLengths_GemmK0_GemmN_GemmK1   = Sequence<1, 2, 8>;
    using GemmBBlockTransferThreadClusterLengths_GemmK0_GemmN_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmBBlockTransferSrcScalarPerVector_GemmK1 = 8;
    constexpr index_t GemmBBlockTransferDstScalarPerVector_GemmK1 = 8;

    constexpr index_t GemmCThreadTransferDstScalarPerVector = 1;
#elif 1
    // [M, N, K0, K1] = [128, 256, 4, 8], C = 128, for fp16
    constexpr index_t BlockSize = 256;

    constexpr index_t GemmMPerBlock = 128;
    constexpr index_t GemmNPerBlock = 256;
    constexpr index_t GemmKPerBlock = 4;

    constexpr index_t GemmMPerXDL = 32;
    constexpr index_t GemmNPerXDL = 32;
    constexpr index_t GemmK1      = 8;

    constexpr index_t MRepeat = 2;
    constexpr index_t NRepeat = 4;

    using GemmABlockTransferThreadSliceLengths_GemmK0_GemmM_GemmK1   = Sequence<1, 2, 8>;
    using GemmABlockTransferThreadClusterLengths_GemmK0_GemmM_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmABlockTransferSrcScalarPerVector_GemmK1 = 8;
    constexpr index_t GemmABlockTransferDstScalarPerVector_GemmK1 = 8;

    using GemmBBlockTransferThreadSliceLengths_GemmK0_GemmN_GemmK1   = Sequence<1, 4, 8>;
    using GemmBBlockTransferThreadClusterLengths_GemmK0_GemmN_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmBBlockTransferSrcScalarPerVector_GemmK1 = 8;
    constexpr index_t GemmBBlockTransferDstScalarPerVector_GemmK1 = 8;

    constexpr index_t GemmCThreadTransferDstScalarPerVector = 1;
#elif 0
    // [M, N, K0, K1] = [128, 128, 4, 8], C = 64, for fp16
    constexpr index_t BlockSize = 256;

    constexpr index_t GemmMPerBlock = 128;
    constexpr index_t GemmNPerBlock = 128;
    constexpr index_t GemmKPerBlock = 4;

    constexpr index_t GemmMPerXDL = 32;
    constexpr index_t GemmNPerXDL = 32;
    constexpr index_t GemmK1      = 8;

    constexpr index_t MRepeat = 2;
    constexpr index_t NRepeat = 2;

    using GemmABlockTransferThreadSliceLengths_GemmK0_GemmM_GemmK1   = Sequence<1, 2, 8>;
    using GemmABlockTransferThreadClusterLengths_GemmK0_GemmM_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmABlockTransferSrcScalarPerVector_GemmK1 = 8;
    constexpr index_t GemmABlockTransferDstScalarPerVector_GemmK1 = 8;

    using GemmBBlockTransferThreadSliceLengths_GemmK0_GemmN_GemmK1   = Sequence<1, 2, 8>;
    using GemmBBlockTransferThreadClusterLengths_GemmK0_GemmN_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmBBlockTransferSrcScalarPerVector_GemmK1 = 8;
    constexpr index_t GemmBBlockTransferDstScalarPerVector_GemmK1 = 8;

    constexpr index_t GemmCThreadTransferDstScalarPerVector = 1;
#elif 0
    // [M, N, K0, K1] = [128, 64, 4, 8], C = 64, for fp16
    constexpr index_t BlockSize = 128;

    constexpr index_t GemmMPerBlock = 128;
    constexpr index_t GemmNPerBlock = 64;
    constexpr index_t GemmKPerBlock = 4;

    constexpr index_t GemmMPerXDL = 32;
    constexpr index_t GemmNPerXDL = 32;
    constexpr index_t GemmK1      = 8;

    constexpr index_t MRepeat = 2;
    constexpr index_t NRepeat = 2;

    using GemmABlockTransferThreadSliceLengths_GemmK0_GemmM_GemmK1   = Sequence<1, 4, 8>;
    using GemmABlockTransferThreadClusterLengths_GemmK0_GemmM_GemmK1 = Sequence<4, 32, 1>;

    constexpr index_t GemmABlockTransferSrcScalarPerVector_GemmK1 = 8;
    constexpr index_t GemmABlockTransferDstScalarPerVector_GemmK1 = 8;

    using GemmBBlockTransferThreadSliceLengths_GemmK0_GemmN_GemmK1   = Sequence<1, 2, 8>;
    using GemmBBlockTransferThreadClusterLengths_GemmK0_GemmN_GemmK1 = Sequence<4, 32, 1>;

    constexpr index_t GemmBBlockTransferSrcScalarPerVector_GemmK1 = 8;
    constexpr index_t GemmBBlockTransferDstScalarPerVector_GemmK1 = 8;

    constexpr index_t GemmCThreadTransferDstScalarPerVector = 1;
#elif 1
    // [M, N, K0, K1] = [128, 64, 4, 8], C = 32, for fp16
    constexpr index_t BlockSize = 256;

    constexpr index_t GemmMPerBlock = 128;
    constexpr index_t GemmNPerBlock = 64;
    constexpr index_t GemmKPerBlock = 4;

    constexpr index_t GemmMPerXDL = 32;
    constexpr index_t GemmNPerXDL = 32;
    constexpr index_t GemmK1      = 8;

    constexpr index_t MRepeat = 2;
    constexpr index_t NRepeat = 1;

    using GemmABlockTransferThreadSliceLengths_GemmK0_GemmM_GemmK1   = Sequence<1, 2, 8>;
    using GemmABlockTransferThreadClusterLengths_GemmK0_GemmM_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmABlockTransferSrcScalarPerVector_GemmK1 = 8;
    constexpr index_t GemmABlockTransferDstScalarPerVector_GemmK1 = 8;

    using GemmBBlockTransferThreadSliceLengths_GemmK0_GemmN_GemmK1   = Sequence<1, 1, 8>;
    using GemmBBlockTransferThreadClusterLengths_GemmK0_GemmN_GemmK1 = Sequence<4, 64, 1>;

    constexpr index_t GemmBBlockTransferSrcScalarPerVector_GemmK1 = 8;
    constexpr index_t GemmBBlockTransferDstScalarPerVector_GemmK1 = 8;

    constexpr index_t GemmCThreadTransferDstScalarPerVector = 1;
#endif

    const auto descs =
        transform_forward_convolution_into_gemm_v4r4r4_nhwc_kyxc_nhwk(in_n_hi_wi_c_desc,
                                                                      wei_k_y_x_c_desc,
                                                                      out_n_ho_wo_k_desc,
                                                                      conv_strides,
                                                                      conv_dilations,
                                                                      in_left_pads,
                                                                      in_right_pads,
                                                                      Number<GemmK1>{});

#if 0 // debug
    const auto in_gemmk0_gemmm_gemmk1_grid_desc  = descs[I0];

    // HACK: hacks that control index calculation when iterating over A matrix
    constexpr auto in_gemmk0_gemmm_gemmk1_grid_step_hacks =
        make_tuple(make_tuple(Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0>{},   // 0+: GemmK0
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0>{},   // 1+: GemmM
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0>{}),  // 2+: GemmK1
                   make_tuple(Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0>{},   // 0-: GemmK0
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0>{},   // 1-: GemmM
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0>{})); // 2-: GemmK1

    constexpr auto in_gemmk0_gemmm_gemmk1_grid_move_slice_window_step_hacks =
        Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 0, 0>{};
#else
    const auto in_gemmk0_gemmmraw_gemmk1_grid_desc          = descs[I0];

    const auto GemmK0   = in_gemmk0_gemmmraw_gemmk1_grid_desc.GetLength(I0);
    const auto GemmMRaw = in_gemmk0_gemmmraw_gemmk1_grid_desc.GetLength(I1);
    const auto GemmMPad = math::integer_least_multiple(GemmMRaw, GemmMPerBlock) - GemmMRaw;

    const auto in_gemmk0_gemmm_gemmk1_grid_desc =
        transform_tensor_descriptor(in_gemmk0_gemmmraw_gemmk1_grid_desc,
                                    make_tuple(make_pass_through_transform(GemmK0),
                                               make_right_pad_transform(GemmMRaw, GemmMPad),
                                               make_pass_through_transform(GemmK1)),
                                    make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}),
                                    make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}));

    // HACK: hacks that control index calculation when iterating over A matrix
    constexpr auto in_gemmk0_gemmm_gemmk1_grid_step_hacks = make_tuple(
        make_tuple(Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0>{},   // 0+: GemmK0
                   Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0>{},   // 1+: GemmM
                   Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0>{}),  // 2+: GemmK1
        make_tuple(Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0>{},   // 0-: GemmK0
                   Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0>{},   // 1-: GemmM
                   Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0>{})); // 2-: GemmK1

    constexpr auto in_gemmk0_gemmm_gemmk1_grid_move_slice_window_step_hacks =
        Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 0>{};
#endif

    const auto wei_gemmk0_gemmn_gemmk1_grid_desc = descs[I1];

    const auto wei_gemmk0_gemmn_gemmk1_grid_step_hacks =
        make_tuple(make_tuple(Sequence<0, 0, 0, 0, 0>{},   // 0+: GemmK0
                              Sequence<0, 0, 0, 0, 0>{},   // 1+: GemmN
                              Sequence<0, 0, 0, 0, 0>{}),  // 2+: GemmK1
                   make_tuple(Sequence<0, 0, 0, 0, 0>{},   // 0-: GemmK0
                              Sequence<0, 0, 0, 0, 0>{},   // 1-: GemmN
                              Sequence<0, 0, 0, 0, 0>{})); // 2-: GemmK1

    constexpr auto wei_gemmk0_gemmn_gemmk1_grid_move_slice_window_step_hacks =
        Sequence<0, 0, 0, 0, 0>{};

#if 0
    const auto out_gemmm_gemmn_grid_desc         = descs[I2];

    constexpr auto out_m0_n0_m1_n1_m2_m3_m4_n2_grid_step_hacks =
        make_tuple(make_tuple(Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 0+: M0
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 1+: N0
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 2+: M1
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 3+: N1
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 4+: M2
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 5+: M3
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 6+: M4
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{}),  // 7+: N2
                   make_tuple(Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 0-: M0
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 1-: N0
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 2-: M1
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 3-: N1
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 4-: M2
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 5-: M3
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 6-: M4
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{})); // 7-: N2
#else
    const auto out_gemmmraw_gemmn_grid_desc = descs[I2];

    const auto GemmN = out_gemmmraw_gemmn_grid_desc.GetLength(I1);

    const auto out_gemmm_gemmn_grid_desc =
        transform_tensor_descriptor(out_gemmmraw_gemmn_grid_desc,
                                    make_tuple(make_right_pad_transform(GemmMRaw, GemmMPad),
                                               make_pass_through_transform(GemmN)),
                                    make_tuple(Sequence<0>{}, Sequence<1>{}),
                                    make_tuple(Sequence<0>{}, Sequence<1>{}));

    constexpr auto out_m0_n0_m1_n1_m2_m3_m4_n2_grid_step_hacks =
        make_tuple(make_tuple(Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 0+: M0
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 1+: N0
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 2+: M1
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 3+: N1
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 4+: M2
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 5+: M3
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 6+: M4
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{}),  // 7+: N2
                   make_tuple(Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 0-: M0
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 1-: N0
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 2-: M1
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 3-: N1
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 4-: M2
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 5-: M3
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},   // 6-: M4
                              Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{})); // 7-: N2
#endif

    for(index_t i = 0; i < 5; ++i)
    {
        float ave_time = driver_gemm_xdlops_v2r3<
            BlockSize,
            TInWei,
            TAcc,
            TOut,
            InMemoryDataOperationEnum_t::Set,
            decltype(in_gemmk0_gemmm_gemmk1_grid_desc),
            decltype(wei_gemmk0_gemmn_gemmk1_grid_desc),
            decltype(out_gemmm_gemmn_grid_desc),
            GemmMPerBlock,
            GemmNPerBlock,
            GemmKPerBlock,
            GemmMPerXDL,
            GemmNPerXDL,
            GemmK1,
            MRepeat,
            NRepeat,
            GemmABlockTransferThreadSliceLengths_GemmK0_GemmM_GemmK1,
            GemmABlockTransferThreadClusterLengths_GemmK0_GemmM_GemmK1,
            Sequence<1, 0, 2>,
            Sequence<1, 0, 2>,
            2,
            GemmABlockTransferSrcScalarPerVector_GemmK1,
            GemmABlockTransferDstScalarPerVector_GemmK1,
            false, // don't move back src coordinate after threadwise copy
            GemmBBlockTransferThreadSliceLengths_GemmK0_GemmN_GemmK1,
            GemmBBlockTransferThreadClusterLengths_GemmK0_GemmN_GemmK1,
            Sequence<1, 0, 2>,
            Sequence<1, 0, 2>,
            2,
            GemmBBlockTransferSrcScalarPerVector_GemmK1,
            GemmBBlockTransferDstScalarPerVector_GemmK1,
            false, // don't move back src coordinate after threadwise copy
            Sequence<2, 3, 0, 1, 7, 5, 4, 6>,
            7,
            GemmCThreadTransferDstScalarPerVector,
            decltype(in_gemmk0_gemmm_gemmk1_grid_step_hacks),
            decltype(wei_gemmk0_gemmn_gemmk1_grid_step_hacks),
            decltype(out_m0_n0_m1_n1_m2_m3_m4_n2_grid_step_hacks),
            decltype(in_gemmk0_gemmm_gemmk1_grid_move_slice_window_step_hacks),
            decltype(wei_gemmk0_gemmn_gemmk1_grid_move_slice_window_step_hacks),
            false, // CAccessOrderMRepeatNRepeat
            true,  // ABlockLdsExtraM
            true   // BBlockLdsExtraN
            >(static_cast<TInWei*>(in_n_hi_wi_c_device_buf.GetDeviceBuffer()),
              static_cast<TInWei*>(wei_k_y_x_c_device_buf.GetDeviceBuffer()),
              static_cast<TOut*>(out_n_ho_wo_k_device_buf.GetDeviceBuffer()),
              in_gemmk0_gemmm_gemmk1_grid_desc,
              wei_gemmk0_gemmn_gemmk1_grid_desc,
              out_gemmm_gemmn_grid_desc,
              debug::debug_driver_gemm_xdlops_v2r3::M01,
              debug::debug_driver_gemm_xdlops_v2r3::N01,
              in_gemmk0_gemmm_gemmk1_grid_step_hacks,
              wei_gemmk0_gemmn_gemmk1_grid_step_hacks,
              out_m0_n0_m1_n1_m2_m3_m4_n2_grid_step_hacks,
              in_gemmk0_gemmm_gemmk1_grid_move_slice_window_step_hacks,
              wei_gemmk0_gemmn_gemmk1_grid_move_slice_window_step_hacks,
              nrepeat);

        {
            const auto N = out_n_ho_wo_k_lengths[I0];
            const auto K = out_n_ho_wo_k_lengths[I3];
            const auto C = wei_k_y_x_c_lengths[I3];

            const auto Ho = out_n_ho_wo_k_lengths[I1];
            const auto Wo = out_n_ho_wo_k_lengths[I2];

            const auto Y = wei_k_y_x_c_lengths[I1];
            const auto X = wei_k_y_x_c_lengths[I2];

            float perf = static_cast<float>((std::size_t(2) * N * K * Ho * Wo * C * Y * X)) /
                         (std::size_t(1000) * 1000 * 1000) / ave_time;

            std::cout << "Average time : " << ave_time << " ms, " << perf << " TFlop/s"
                      << std::endl;
        }
    }

    // copy result back to host
    out_n_ho_wo_k_device_buf.FromDevice(out_n_ho_wo_k.mData.data());
}
