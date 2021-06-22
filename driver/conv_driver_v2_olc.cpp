#include <iostream>
#include <numeric>
#include <initializer_list>
#include <cstdlib>
#include <stdlib.h>
#include <half.hpp>
#include "config.hpp"
#include "print.hpp"
#include "device.hpp"
#include "host_tensor.hpp"
#include "host_tensor_generator.hpp"
#include "conv_common.hpp"
#include "host_conv.hpp"
#include "device_tensor.hpp"
#include "olc_device_dynamic_convolution_forward_implicit_gemm_v4r4_nchw_kcyx_nkhw.hpp"
#include "olc_device_dynamic_convolution_forward_implicit_gemm_v4r5_nchw_kcyx_nkhw.hpp"

#include "conv_tunables.hpp"
#include "handle.hpp"
#include "hipCheck.hpp"

int main(int argc, char* argv[])
{
    using namespace ck;
    using size_t = std::size_t;

    hipStream_t stream;
    olCompile::Handle* handle;

    MY_HIP_CHECK(hipStreamCreate(&stream));

    handle = new olCompile::Handle(stream);

    if(argc != 21)
    {
        printf("arg1: layout, arg2: do_verification, arg3: do_log, arg4: init_method, arg5: "
               "nrepeat\n");
        printf("rest: N, K, C, Y, X, Hi, Wi, Sy, Sx, Dy, Dx, LeftPy, LeftPx, RightPy, RightPx\n");
        exit(1);
    }

    constexpr auto I0 = Number<0>{};
    constexpr auto I1 = Number<1>{};
    constexpr auto I2 = Number<2>{};
    constexpr auto I3 = Number<3>{};

    const ConvTensorLayout layout =
        atoi(argv[1]) == 0 ? ConvTensorLayout::NCHW : ConvTensorLayout::NHWC;
    const bool do_verification = atoi(argv[2]);
    const int init_method      = atoi(argv[3]);
    const bool do_log          = atoi(argv[4]);
    const int nrepeat          = atoi(argv[5]);

    const index_t N  = atoi(argv[6]);
    const index_t K  = atoi(argv[7]);
    const index_t C  = atoi(argv[8]);
    const index_t Y  = atoi(argv[9]);
    const index_t X  = atoi(argv[10]);
    const index_t Hi = atoi(argv[11]);
    const index_t Wi = atoi(argv[12]);

    const auto conv_strides   = make_tuple(atoi(argv[13]), atoi(argv[14]));
    const auto conv_dilations = make_tuple(atoi(argv[15]), atoi(argv[16]));
    const auto in_left_pads   = make_tuple(atoi(argv[17]), atoi(argv[18]));
    const auto in_right_pads  = make_tuple(atoi(argv[19]), atoi(argv[20]));

    const auto YEff = (Y - 1) * conv_dilations[I0] + 1;
    const auto XEff = (X - 1) * conv_dilations[I1] + 1;

    const auto Ho = (Hi + in_left_pads[I0] + in_right_pads[I0] - YEff) / conv_strides[I0] + 1;
    const auto Wo = (Wi + in_left_pads[I1] + in_right_pads[I1] - XEff) / conv_strides[I1] + 1;

#if 1
    constexpr index_t in_vector_size = 1;
    using in_data_t                  = typename vector_type<float, in_vector_size>::type;
    using acc_data_t                 = float;
    using out_data_t                 = float;
#elif 0
    constexpr index_t in_vector_size = 1;
    using in_data_t                  = typename vector_type<float, in_vector_size>::type;
    using acc_data_t                 = float;
    using out_data_t                 = int8_t;
#elif 1
    constexpr index_t in_vector_size = 16;
    using in_data_t                  = typename vector_type<int8_t, in_vector_size>::type;
    using acc_data_t                 = int32_t;
    using out_data_t                 = int8_t;
#endif

    std::vector<size_t> in_lengths, wei_lengths, out_lengths;

    switch(layout)
    {
    case ConvTensorLayout::NCHW:
        // NCHW
        in_lengths  = std::initializer_list<size_t>{static_cast<size_t>(N),
                                                   static_cast<size_t>(C),
                                                   static_cast<size_t>(Hi),
                                                   static_cast<size_t>(Wi)};
        wei_lengths = std::initializer_list<size_t>{static_cast<size_t>(K),
                                                    static_cast<size_t>(C),
                                                    static_cast<size_t>(Y),
                                                    static_cast<size_t>(X)};
        out_lengths = std::initializer_list<size_t>{static_cast<size_t>(N),
                                                    static_cast<size_t>(K),
                                                    static_cast<size_t>(Ho),
                                                    static_cast<size_t>(Wo)};
        break;
    case ConvTensorLayout::NHWC:
        // NCHW
        in_lengths  = std::initializer_list<size_t>{static_cast<size_t>(N),
                                                   static_cast<size_t>(Hi),
                                                   static_cast<size_t>(Wi),
                                                   static_cast<size_t>(C)};
        wei_lengths = std::initializer_list<size_t>{static_cast<size_t>(K),
                                                    static_cast<size_t>(Y),
                                                    static_cast<size_t>(X),
                                                    static_cast<size_t>(C)};
        out_lengths = std::initializer_list<size_t>{static_cast<size_t>(N),
                                                    static_cast<size_t>(Ho),
                                                    static_cast<size_t>(Wo),
                                                    static_cast<size_t>(K)};
        break;
    default:
        in_lengths  = std::initializer_list<size_t>{static_cast<size_t>(N),
                                                   static_cast<size_t>(C),
                                                   static_cast<size_t>(Hi),
                                                   static_cast<size_t>(Wi)};
        wei_lengths = std::initializer_list<size_t>{static_cast<size_t>(K),
                                                    static_cast<size_t>(C),
                                                    static_cast<size_t>(Y),
                                                    static_cast<size_t>(X)};
        out_lengths = std::initializer_list<size_t>{static_cast<size_t>(N),
                                                    static_cast<size_t>(K),
                                                    static_cast<size_t>(Ho),
                                                    static_cast<size_t>(Wo)};
    }

    Tensor<in_data_t> in(in_lengths);
    Tensor<in_data_t> wei(wei_lengths);
    Tensor<out_data_t> out_host(out_lengths);
    Tensor<out_data_t> out_device(out_lengths);

    std::cout << "layout: " << layout << std::endl;
    ostream_HostTensorDescriptor(in.mDesc, std::cout << "in: ");
    ostream_HostTensorDescriptor(wei.mDesc, std::cout << "wei: ");
    ostream_HostTensorDescriptor(out_host.mDesc, std::cout << "out: ");
    print_array("InLeftPads", in_left_pads);
    print_array("InRightPads", in_right_pads);
    print_array("ConvStrides", conv_strides);
    print_array("ConvDilations", conv_dilations);

    std::size_t num_thread = std::thread::hardware_concurrency();

    if(do_verification)
    {
        switch(init_method)
        {
        case 0:
            in.GenerateTensorValue(GeneratorTensor_1{}, num_thread);
            wei.GenerateTensorValue(GeneratorTensor_1{}, num_thread);
            break;
        case 1:
            in.GenerateTensorValue(GeneratorTensor_1{}, num_thread);
            wei.GenerateTensorValue(GeneratorTensor_2{-5, 5}, num_thread);
            break;
        case 2:
            in.GenerateTensorValue(GeneratorTensor_2{-5, 5}, num_thread);
            wei.GenerateTensorValue(GeneratorTensor_1{}, num_thread);
            break;
        case 3:
            in.GenerateTensorValue(GeneratorTensor_2{-5, 5}, num_thread);
            wei.GenerateTensorValue(GeneratorTensor_2{-5, 5}, num_thread);
            break;
        default:
            in.GenerateTensorValue(GeneratorTensor_2{1, 5}, num_thread);

            auto gen_wei = [](auto... is) {
                return GeneratorTensor_2{1, 5}(is...) * GeneratorTensor_Checkboard{}(is...);
            };
            wei.GenerateTensorValue(gen_wei, num_thread);
        }
    }

#if 1
    tunable_dyn_conv_fwd_v4r4_nchw_kcyx_nkhw* tunable =
        &default_tunable_dyn_conv_fwd_v4r4_nchw_kcyx_nkhw;

    device_dynamic_convolution_forward_implicit_gemm_v4r4_nchw_kcyx_nkhw_olc<in_data_t,
                                                                             acc_data_t,
                                                                             out_data_t>(
        handle,
        in_lengths,
        wei_lengths,
        out_lengths,
        conv_strides,
        conv_dilations,
        in_left_pads,
        in_right_pads,
        in,
        wei,
        out_device,
        tunable,
        nrepeat);
#elif 0
    tunable_dyn_conv_fwd_v4r5_nchw_kcyx_nkhw* tunable =
        &default_tunable_dyn_conv_fwd_v4r5_nchw_kcyx_nkhw;

    device_dynamic_convolution_forward_implicit_gemm_v4r5_nchw_kcyx_nkhw_olc<in_data_t,
                                                                             1,
                                                                             acc_data_t,
                                                                             out_data_t>(
        handle,
        in_lengths,
        wei_lengths,
        out_lengths,
        conv_strides,
        conv_dilations,
        in_left_pads,
        in_right_pads,
        in,
        wei,
        out_device,
        tunable,
        nrepeat);
#endif

    if(do_verification)
    {
        host_direct_convolution(
            in, wei, out_host, conv_strides, conv_dilations, in_left_pads, in_right_pads, layout);

        check_error(out_host, out_device);

        if(do_log)
        {
            LogRange(std::cout << "in : ", in.mData, ",") << std::endl;
            LogRange(std::cout << "wei: ", wei.mData, ",") << std::endl;
            LogRange(std::cout << "out_host  : ", out_host.mData, ",") << std::endl;
            LogRange(std::cout << "out_device: ", out_device.mData, ",") << std::endl;
        }
    }

    delete handle;
    MY_HIP_CHECK(hipStreamDestroy(stream));
}
