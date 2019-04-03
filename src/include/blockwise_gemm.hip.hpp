#pragma once
#include "threadwise_gemm.hip.hpp"

// if following number are power of 2, index calculation shall be greatly reduced:
//    MPerThreadSubC, NPerThreadSubC, MLevel0Cluster, NLevel0Cluster, MLevel1Cluster, NLevel1Cluster
template <index_t BlockSize,
          class BlockMatrixA,
          class BlockMatrixB,
          class ThreadMatrixC,
          index_t MPerThreadSubC,
          index_t NPerThreadSubC,
          index_t MLevel0Cluster,
          index_t NLevel0Cluster,
          index_t MLevel1Cluster,
          index_t NLevel1Cluster,
          index_t KPerThreadLoop>
struct BlockwiseGemmBlockABlockBThreadCTransANormalBNormalC_v2
{
    struct MatrixIndex
    {
        index_t row;
        index_t col;
    };

    index_t mMyThreadOffsetA;
    index_t mMyThreadOffsetB;

    __device__ BlockwiseGemmBlockABlockBThreadCTransANormalBNormalC_v2()
    {
        constexpr index_t ThreadPerLevel1Cluster =
            MLevel0Cluster * NLevel0Cluster * MLevel1Cluster * NLevel1Cluster;

        static_assert(BlockSize == ThreadPerLevel1Cluster, "wrong! wrong blocksize\n");

        constexpr auto a_block_mtx  = BlockMatrixA{};
        constexpr auto b_block_mtx  = BlockMatrixB{};
        constexpr auto c_thread_mtx = ThreadMatrixC{};

        static_assert(a_block_mtx.NRow() == b_block_mtx.NRow(),
                      "wrong! K dimension not consistent\n");

        constexpr index_t M = a_block_mtx.NCol(); // A is transposed
        constexpr index_t N = b_block_mtx.NCol();
        constexpr index_t K = a_block_mtx.NRow();

        constexpr index_t MPerThread = c_thread_mtx.NRow();
        constexpr index_t NPerThread = c_thread_mtx.NCol();

        static_assert((MPerThread % MPerThreadSubC == 0) && (NPerThread % NPerThreadSubC == 0),
                      "wrong! Cannot evenly divide thread work among repeat \n");

        constexpr index_t MRepeat = MPerThread / MPerThreadSubC;
        constexpr index_t NRepeat = NPerThread / NPerThreadSubC;

        static_assert((M % MRepeat == 0) && (N % NRepeat == 0),
                      "wrong! Cannot evenly divide work among repeat\n");

        constexpr index_t MPerLevel1Cluster = M / MRepeat;
        constexpr index_t NPerLevel1Cluster = N / NRepeat;

        static_assert((MPerLevel1Cluster % MLevel1Cluster == 0) &&
                          (NPerLevel1Cluster % NLevel1Cluster == 0),
                      "wrong! Cannot evenly divide work among Level1Cluster\n");

        constexpr index_t MPerLevel0Cluster = MPerLevel1Cluster / MLevel1Cluster;
        constexpr index_t NPerLevel0Cluster = NPerLevel1Cluster / NLevel1Cluster;

        static_assert((MPerLevel0Cluster % MLevel0Cluster == 0) &&
                          (NPerLevel0Cluster % NLevel0Cluster == 0),
                      "wrong! Cannot evenly divide work among Level0Cluster\n");

        static_assert((MPerThreadSubC == MPerLevel0Cluster / MLevel0Cluster) &&
                          (NPerThreadSubC == NPerLevel0Cluster / NLevel0Cluster),
                      "wrong! thread work size is wrong\n");

        auto c_thread_mtx_index = GetBeginOfThreadMatrixC(get_thread_local_1d_id());

        mMyThreadOffsetA = a_block_mtx.Get1dIndex(0, c_thread_mtx_index.row);
        mMyThreadOffsetB = b_block_mtx.Get1dIndex(0, c_thread_mtx_index.col);
    }

    __device__ static MatrixIndex GetBeginOfThreadMatrixC(index_t thread_id)
    {
        constexpr index_t ThreadPerLevel0Cluster = MLevel0Cluster * NLevel0Cluster;

        index_t level1_id   = thread_id / ThreadPerLevel0Cluster;
        index_t level1_m_id = level1_id / NLevel1Cluster;
        index_t level1_n_id = level1_id % NLevel1Cluster;

        index_t level0_id   = thread_id % ThreadPerLevel0Cluster;
        index_t level0_m_id = level0_id / NLevel0Cluster;
        index_t level0_n_id = level0_id % NLevel0Cluster;

        constexpr index_t MPerLevel0Cluster = MPerThreadSubC * MLevel0Cluster;
        constexpr index_t NPerLevel0Cluster = NPerThreadSubC * NLevel0Cluster;

        return MatrixIndex{level1_m_id * MPerLevel0Cluster + level0_m_id * MPerThreadSubC,
                           level1_n_id * NPerLevel0Cluster + level0_n_id * NPerThreadSubC};
    }

    // this should be optimized away if input is known
    __device__ static MatrixIndex GetDistanceFromBeginOfThreadMatrixC(index_t m_in_c,
                                                                      index_t n_in_c)
    {
        constexpr auto c_thread_mtx = ThreadMatrixC{};

        constexpr index_t MPerThread = c_thread_mtx.NRow();
        constexpr index_t NPerThread = c_thread_mtx.NCol();

        constexpr index_t MRepeat = MPerThread / MPerThreadSubC;
        constexpr index_t NRepeat = NPerThread / NPerThreadSubC;

        constexpr index_t MPerLevel1Cluster = MPerThreadSubC * MLevel0Cluster * MLevel1Cluster;
        constexpr index_t NPerLevel1Cluster = NPerThreadSubC * NLevel0Cluster * NLevel1Cluster;

        index_t m_repeat = m_in_c / MPerThreadSubC;
        index_t n_repeat = n_in_c / NPerThreadSubC;

        index_t m_in_sub_c = m_in_c % MPerThreadSubC;
        index_t n_in_sub_c = n_in_c % NPerThreadSubC;

        return MatrixIndex{m_repeat * MPerLevel1Cluster + m_in_sub_c,
                           n_repeat * NPerLevel1Cluster + n_in_sub_c};
    }

    template <class FloatA, class FloatB, class FloatC, class Accumulator>
    __device__ void Run_asm(const FloatA* __restrict__ p_a_block,
                            const FloatB* __restrict__ p_b_block,
                            FloatC* __restrict__ p_c_thread,
                            Accumulator f_accum) const
    {
#if DEVICE_BACKEND_HIP
        constexpr auto True  = integral_constant<bool, true>{};
        constexpr auto False = integral_constant<bool, false>{};

        constexpr auto a_block_mtx  = BlockMatrixA{};
        constexpr auto b_block_mtx  = BlockMatrixB{};
        constexpr auto c_thread_mtx = ThreadMatrixC{};

        constexpr index_t M = a_block_mtx.NCol();
        constexpr index_t N = b_block_mtx.NCol();
        constexpr index_t K = a_block_mtx.NRow();

        constexpr index_t MPerThread = c_thread_mtx.NRow();
        constexpr index_t NPerThread = c_thread_mtx.NCol();

        // thread A, B for GEMM
        constexpr auto a_thread_mtx =
            make_ConstantMatrixDescriptor(Number<KPerThreadLoop>{}, Number<MPerThread>{});

        constexpr auto b_thread_mtx =
            make_ConstantMatrixDescriptor(Number<KPerThreadLoop>{}, Number<NPerThread>{});

        // thread A-sub, B-sub for copy
        constexpr auto a_thread_sub_mtx = make_ConstantMatrixDescriptor(
            Number<KPerThreadLoop>{}, Number<MPerThreadSubC>{}, Number<MPerThread>{});

        constexpr auto b_thread_sub_mtx = make_ConstantMatrixDescriptor(
            Number<KPerThreadLoop>{}, Number<NPerThreadSubC>{}, Number<NPerThread>{});

        float p_thread[a_thread_mtx.GetElementSpace() + b_thread_mtx.GetElementSpace()];

        FloatA* p_a_thread = p_thread;
        FloatB* p_b_thread = p_thread + a_thread_mtx.GetElementSpace();

        constexpr index_t MPerLevel1Cluster = MPerThreadSubC * MLevel0Cluster * MLevel1Cluster;
        constexpr index_t NPerLevel1Cluster = NPerThreadSubC * NLevel0Cluster * NLevel1Cluster;

        constexpr index_t MRepeat = MPerThread / MPerThreadSubC;
        constexpr index_t NRepeat = NPerThread / NPerThreadSubC;

#pragma unroll
        // loop over k
        for(index_t k_begin = 0; k_begin < K; k_begin += KPerThreadLoop)
        {
#if 1
            auto a_src_index = a_block_mtx.Get1dIndex(k_begin, 0) + mMyThreadOffsetA;
            auto b_src_index = b_block_mtx.Get1dIndex(k_begin, 0) + mMyThreadOffsetB;

            const float4* a_loc = (const float4*)(p_a_block + a_src_index);
            const float4* b_loc = (const float4*)(p_b_block + b_src_index);
            float4* reg         = (float4*)(p_thread);

            reg[0] = a_loc[0];
            reg[1] = a_loc[16];
            reg[2] = b_loc[0];
            reg[3] = b_loc[8];

            //asm volatile("\n \
                    //ds_read2_b64 %0, %1 offset1:1 \n \
                    //s_waitcnt lgkmcnt(0)"
            //: "=v"(reg[0])
            //: "v"(__to_local((void *)(a_loc)))
            //);

            //asm volatile("\n \
                    //ds_read2_b64 %0, %1 offset1:1 \n \
                    //s_waitcnt lgkmcnt(0)"
            //: "=v"(reg[1])
            //: "v"(__to_local((void *)(a_loc + 16)))
            //);

            //asm volatile("\n \
                    //ds_read2_b64 %0, %1 offset1:1 \n \
                    //s_waitcnt lgkmcnt(0)"
            //: "=v"(reg[2])
            //: "v"(__to_local((void *)(b_loc)))
            //);

            //asm volatile("\n \
                    //ds_read2_b64 %0, %1 offset1:1 \n \
                    //s_waitcnt lgkmcnt(0)"
            //: "=v"(reg[3])
            //: "v"(__to_local((void *)(b_loc + 8)))
            //);

            //asm volatile("\n \
                    //ds_read2_b64 %0, %4 offset1:1 \n \
                    //ds_read2_b64 %1, %4 offset0:32 offset1:33 \n \
                    //ds_read2_b64 %2, %5 offset1:1 \n \
                    //ds_read2_b64 %3, %5 offset0:16 offset1:17 \n \
                    //s_waitcnt lgkmcnt(0)"
            //: "=v"(reg[0]), "=v"(reg[1]), "=v"(reg[2]), "=v"(reg[3])
            //: "v"(__to_local((void *)(a_loc))), "v"(__to_local((void *)(b_loc)))
            //);

            //asm volatile("\n \
                    //ds_read_b32  %0, %16 \n \
                    //ds_read_b32  %1, %16 offset:1\n \
                    //ds_read_b32  %2, %16 offset:2\n \
                    //ds_read_b32  %3, %16 offset:3\n \
                    //ds_read_b32  %4, %17 \n \
                    //ds_read_b32  %5, %17 offset:1\n \
                    //ds_read_b32  %6, %17 offset:2\n \
                    //ds_read_b32  %7, %17 offset:3\n \
                    //ds_read_b32  %8, %18 \n \
                    //ds_read_b32  %9, %18 offset:1\n \
                    //ds_read_b32 %10, %18 offset:2\n \
                    //ds_read_b32 %11, %18 offset:3\n \
                    //ds_read_b32 %12, %19 \n \
                    //ds_read_b32 %13, %19 offset:1\n \
                    //ds_read_b32 %14, %19 offset:2\n \
                    //ds_read_b32 %15, %19 offset:3\n \
                    //s_waitcnt lgkmcnt(0)"
            //:
            //"=v"(p_a_thread[0]),
            //"=v"(p_a_thread[1]),
            //"=v"(p_a_thread[2]),
            //"=v"(p_a_thread[3]),
            //"=v"(p_a_thread[4]),
            //"=v"(p_a_thread[5]),
            //"=v"(p_a_thread[6]),
            //"=v"(p_a_thread[7]),
            //"=v"(p_b_thread[0]),
            //"=v"(p_b_thread[1]),
            //"=v"(p_b_thread[2]),
            //"=v"(p_b_thread[3]),
            //"=v"(p_b_thread[4]),
            //"=v"(p_b_thread[5]),
            //"=v"(p_b_thread[6]),
            //"=v"(p_b_thread[7])
            //:
            //"v"(__to_local((void *)(&p_a_block[0]))),
            //"v"(__to_local((void *)(&p_a_block[64]))),
            //"v"(__to_local((void *)(&p_b_block[0]))),
            //"v"(__to_local((void *)(&p_b_block[32])))
            //);

            // C = A * B
            asm volatile("\n \
                    v_mac_f32 %0, %64, %72 \n \
                    v_mac_f32 %1, %64, %73 \n \
                    v_mac_f32 %2, %64, %74 \n \
                    v_mac_f32 %3, %64, %75 \n \
                    v_mac_f32 %4, %64, %76 \n \
                    v_mac_f32 %5, %64, %77 \n \
                    v_mac_f32 %6, %64, %78 \n \
                    v_mac_f32 %7, %64, %79 \n \
                    v_mac_f32 %8, %65, %72 \n \
                    v_mac_f32 %9, %65, %73 \n \
                    v_mac_f32 %10, %65, %74 \n \
                    v_mac_f32 %11, %65, %75 \n \
                    v_mac_f32 %12, %65, %76 \n \
                    v_mac_f32 %13, %65, %77 \n \
                    v_mac_f32 %14, %65, %78 \n \
                    v_mac_f32 %15, %65, %79 \n \
                    v_mac_f32 %16, %66, %72 \n \
                    v_mac_f32 %17, %66, %73 \n \
                    v_mac_f32 %18, %66, %74 \n \
                    v_mac_f32 %19, %66, %75 \n \
                    v_mac_f32 %20, %66, %76 \n \
                    v_mac_f32 %21, %66, %77 \n \
                    v_mac_f32 %22, %66, %78 \n \
                    v_mac_f32 %23, %66, %79 \n \
                    v_mac_f32 %24, %67, %72 \n \
                    v_mac_f32 %25, %67, %73 \n \
                    v_mac_f32 %26, %67, %74 \n \
                    v_mac_f32 %27, %67, %75 \n \
                    v_mac_f32 %28, %67, %76 \n \
                    v_mac_f32 %29, %67, %77 \n \
                    v_mac_f32 %30, %67, %78 \n \
                    v_mac_f32 %31, %67, %79 \n \
                    v_mac_f32 %32, %68, %72 \n \
                    v_mac_f32 %33, %68, %73 \n \
                    v_mac_f32 %34, %68, %74 \n \
                    v_mac_f32 %35, %68, %75 \n \
                    v_mac_f32 %36, %68, %76 \n \
                    v_mac_f32 %37, %68, %77 \n \
                    v_mac_f32 %38, %68, %78 \n \
                    v_mac_f32 %39, %68, %79 \n \
                    v_mac_f32 %40, %69, %72 \n \
                    v_mac_f32 %41, %69, %73 \n \
                    v_mac_f32 %42, %69, %74 \n \
                    v_mac_f32 %43, %69, %75 \n \
                    v_mac_f32 %44, %69, %76 \n \
                    v_mac_f32 %45, %69, %77 \n \
                    v_mac_f32 %46, %69, %78 \n \
                    v_mac_f32 %47, %69, %79 \n \
                    v_mac_f32 %48, %70, %72 \n \
                    v_mac_f32 %49, %70, %73 \n \
                    v_mac_f32 %50, %70, %74 \n \
                    v_mac_f32 %51, %70, %75 \n \
                    v_mac_f32 %52, %70, %76 \n \
                    v_mac_f32 %53, %70, %77 \n \
                    v_mac_f32 %54, %70, %78 \n \
                    v_mac_f32 %55, %70, %79 \n \
                    v_mac_f32 %56, %71, %72 \n \
                    v_mac_f32 %57, %71, %73 \n \
                    v_mac_f32 %58, %71, %74 \n \
                    v_mac_f32 %59, %71, %75 \n \
                    v_mac_f32 %60, %71, %76 \n \
                    v_mac_f32 %61, %71, %77 \n \
                    v_mac_f32 %62, %71, %78 \n \
                    v_mac_f32 %63, %71, %79 \n \
                    "
                         : "=v"(p_c_thread[0]),
                           "=v"(p_c_thread[1]),
                           "=v"(p_c_thread[2]),
                           "=v"(p_c_thread[3]),
                           "=v"(p_c_thread[4]),
                           "=v"(p_c_thread[5]),
                           "=v"(p_c_thread[6]),
                           "=v"(p_c_thread[7]),
                           "=v"(p_c_thread[8]),
                           "=v"(p_c_thread[9]),
                           "=v"(p_c_thread[10]),
                           "=v"(p_c_thread[11]),
                           "=v"(p_c_thread[12]),
                           "=v"(p_c_thread[13]),
                           "=v"(p_c_thread[14]),
                           "=v"(p_c_thread[15]),
                           "=v"(p_c_thread[16]),
                           "=v"(p_c_thread[17]),
                           "=v"(p_c_thread[18]),
                           "=v"(p_c_thread[19]),
                           "=v"(p_c_thread[20]),
                           "=v"(p_c_thread[21]),
                           "=v"(p_c_thread[22]),
                           "=v"(p_c_thread[23]),
                           "=v"(p_c_thread[24]),
                           "=v"(p_c_thread[25]),
                           "=v"(p_c_thread[26]),
                           "=v"(p_c_thread[27]),
                           "=v"(p_c_thread[28]),
                           "=v"(p_c_thread[29]),
                           "=v"(p_c_thread[30]),
                           "=v"(p_c_thread[31]),
                           "=v"(p_c_thread[32]),
                           "=v"(p_c_thread[33]),
                           "=v"(p_c_thread[34]),
                           "=v"(p_c_thread[35]),
                           "=v"(p_c_thread[36]),
                           "=v"(p_c_thread[37]),
                           "=v"(p_c_thread[38]),
                           "=v"(p_c_thread[39]),
                           "=v"(p_c_thread[40]),
                           "=v"(p_c_thread[41]),
                           "=v"(p_c_thread[42]),
                           "=v"(p_c_thread[43]),
                           "=v"(p_c_thread[44]),
                           "=v"(p_c_thread[45]),
                           "=v"(p_c_thread[46]),
                           "=v"(p_c_thread[47]),
                           "=v"(p_c_thread[48]),
                           "=v"(p_c_thread[49]),
                           "=v"(p_c_thread[50]),
                           "=v"(p_c_thread[51]),
                           "=v"(p_c_thread[52]),
                           "=v"(p_c_thread[53]),
                           "=v"(p_c_thread[54]),
                           "=v"(p_c_thread[55]),
                           "=v"(p_c_thread[56]),
                           "=v"(p_c_thread[57]),
                           "=v"(p_c_thread[58]),
                           "=v"(p_c_thread[59]),
                           "=v"(p_c_thread[60]),
                           "=v"(p_c_thread[61]),
                           "=v"(p_c_thread[62]),
                           "=v"(p_c_thread[63])
                         : "v"(p_a_thread[0]),
                           "v"(p_a_thread[1]),
                           "v"(p_a_thread[2]),
                           "v"(p_a_thread[3]),
                           "v"(p_a_thread[4]),
                           "v"(p_a_thread[5]),
                           "v"(p_a_thread[6]),
                           "v"(p_a_thread[7]),
                           "v"(p_b_thread[0]),
                           "v"(p_b_thread[1]),
                           "v"(p_b_thread[2]),
                           "v"(p_b_thread[3]),
                           "v"(p_b_thread[4]),
                           "v"(p_b_thread[5]),
                           "v"(p_b_thread[6]),
                           "v"(p_b_thread[7]),
                           "0"(p_c_thread[0]),
                           "1"(p_c_thread[1]),
                           "2"(p_c_thread[2]),
                           "3"(p_c_thread[3]),
                           "4"(p_c_thread[4]),
                           "5"(p_c_thread[5]),
                           "6"(p_c_thread[6]),
                           "7"(p_c_thread[7]),
                           "8"(p_c_thread[8]),
                           "9"(p_c_thread[9]),
                           "10"(p_c_thread[10]),
                           "11"(p_c_thread[11]),
                           "12"(p_c_thread[12]),
                           "13"(p_c_thread[13]),
                           "14"(p_c_thread[14]),
                           "15"(p_c_thread[15]),
                           "16"(p_c_thread[16]),
                           "17"(p_c_thread[17]),
                           "18"(p_c_thread[18]),
                           "19"(p_c_thread[19]),
                           "20"(p_c_thread[20]),
                           "21"(p_c_thread[21]),
                           "22"(p_c_thread[22]),
                           "23"(p_c_thread[23]),
                           "24"(p_c_thread[24]),
                           "25"(p_c_thread[25]),
                           "26"(p_c_thread[26]),
                           "27"(p_c_thread[27]),
                           "28"(p_c_thread[28]),
                           "29"(p_c_thread[29]),
                           "30"(p_c_thread[30]),
                           "31"(p_c_thread[31]),
                           "32"(p_c_thread[32]),
                           "33"(p_c_thread[33]),
                           "34"(p_c_thread[34]),
                           "35"(p_c_thread[35]),
                           "36"(p_c_thread[36]),
                           "37"(p_c_thread[37]),
                           "38"(p_c_thread[38]),
                           "39"(p_c_thread[39]),
                           "40"(p_c_thread[40]),
                           "41"(p_c_thread[41]),
                           "42"(p_c_thread[42]),
                           "43"(p_c_thread[43]),
                           "44"(p_c_thread[44]),
                           "45"(p_c_thread[45]),
                           "46"(p_c_thread[46]),
                           "47"(p_c_thread[47]),
                           "48"(p_c_thread[48]),
                           "49"(p_c_thread[49]),
                           "50"(p_c_thread[50]),
                           "51"(p_c_thread[51]),
                           "52"(p_c_thread[52]),
                           "53"(p_c_thread[53]),
                           "54"(p_c_thread[54]),
                           "55"(p_c_thread[55]),
                           "56"(p_c_thread[56]),
                           "57"(p_c_thread[57]),
                           "58"(p_c_thread[58]),
                           "59"(p_c_thread[59]),
                           "60"(p_c_thread[60]),
                           "61"(p_c_thread[61]),
                           "62"(p_c_thread[62]),
                           "63"(p_c_thread[63]));

#else
            auto a_src_index = a_block_mtx.Get1dIndex(k_begin, 0) + mMyThreadOffsetA;
            auto b_src_index = b_block_mtx.Get1dIndex(k_begin, 0) + mMyThreadOffsetB;
            auto dst_index   = a_thread_sub_mtx.Get1dIndex(0, 0);

            const float4* a_loc = (const float4*)(p_a_block + a_src_index);
            const float4* b_loc = (const float4*)(p_b_block + b_src_index);
            float4* reg         = (float4*)(p_a_thread + dst_index);

            asm volatile("\n \
                                ds_read2_b64 %0, %84 offset1:1 \n \
                                ds_read2_b64 %1, %84 offset0:32 offset1:33 \n \
                                ds_read2_b64 %2, %85 offset1:1 \n \
                                ds_read2_b64 %3, %85 offset0:16 offset1:17 \n \
                                s_waitcnt lgkmcnt(0) \n \
                                v_mac_f32 %4, %68, %76 \n \
                                v_mac_f32 %5, %68, %77 \n \
                                v_mac_f32 %6, %68, %78 \n \
                                v_mac_f32 %7, %68, %79 \n \
                                v_mac_f32 %8, %68, %80 \n \
                                v_mac_f32 %9, %68, %81 \n \
                                v_mac_f32 %10, %68, %82 \n \
                                v_mac_f32 %11, %68, %83 \n \
                                v_mac_f32 %12, %69, %76 \n \
                                v_mac_f32 %13, %69, %77 \n \
                                v_mac_f32 %14, %69, %78 \n \
                                v_mac_f32 %15, %69, %79 \n \
                                v_mac_f32 %16, %69, %80 \n \
                                v_mac_f32 %17, %69, %81 \n \
                                v_mac_f32 %18, %69, %82 \n \
                                v_mac_f32 %19, %69, %83 \n \
                                v_mac_f32 %20, %70, %76 \n \
                                v_mac_f32 %21, %70, %77 \n \
                                v_mac_f32 %22, %70, %78 \n \
                                v_mac_f32 %23, %70, %79 \n \
                                v_mac_f32 %24, %70, %80 \n \
                                v_mac_f32 %25, %70, %81 \n \
                                v_mac_f32 %26, %70, %82 \n \
                                v_mac_f32 %27, %70, %83 \n \
                                v_mac_f32 %28, %71, %76 \n \
                                v_mac_f32 %29, %71, %77 \n \
                                v_mac_f32 %30, %71, %78 \n \
                                v_mac_f32 %31, %71, %79 \n \
                                v_mac_f32 %32, %71, %80 \n \
                                v_mac_f32 %33, %71, %81 \n \
                                v_mac_f32 %34, %71, %82 \n \
                                v_mac_f32 %35, %71, %83 \n \
                                v_mac_f32 %36, %72, %76 \n \
                                v_mac_f32 %37, %72, %77 \n \
                                v_mac_f32 %38, %72, %78 \n \
                                v_mac_f32 %39, %72, %79 \n \
                                v_mac_f32 %40, %72, %80 \n \
                                v_mac_f32 %41, %72, %81 \n \
                                v_mac_f32 %42, %72, %82 \n \
                                v_mac_f32 %43, %72, %83 \n \
                                v_mac_f32 %44, %73, %76 \n \
                                v_mac_f32 %45, %73, %77 \n \
                                v_mac_f32 %46, %73, %78 \n \
                                v_mac_f32 %47, %73, %79 \n \
                                v_mac_f32 %48, %73, %80 \n \
                                v_mac_f32 %49, %73, %81 \n \
                                v_mac_f32 %50, %73, %82 \n \
                                v_mac_f32 %51, %73, %83 \n \
                                v_mac_f32 %52, %74, %76 \n \
                                v_mac_f32 %53, %74, %77 \n \
                                v_mac_f32 %54, %74, %78 \n \
                                v_mac_f32 %55, %74, %79 \n \
                                v_mac_f32 %56, %74, %80 \n \
                                v_mac_f32 %57, %74, %81 \n \
                                v_mac_f32 %58, %74, %82 \n \
                                v_mac_f32 %59, %74, %83 \n \
                                v_mac_f32 %60, %75, %76 \n \
                                v_mac_f32 %61, %75, %77 \n \
                                v_mac_f32 %62, %75, %78 \n \
                                v_mac_f32 %63, %75, %79 \n \
                                v_mac_f32 %64, %75, %80 \n \
                                v_mac_f32 %65, %75, %81 \n \
                                v_mac_f32 %66, %75, %82 \n \
                                v_mac_f32 %67, %75, %83 \n \
                                "
                         : "=v"(reg[0]),
                           "=v"(reg[1]),
                           "=v"(reg[2]),
                           "=v"(reg[3]),
                           "=v"(p_c_thread[0]),
                           "=v"(p_c_thread[1]),
                           "=v"(p_c_thread[2]),
                           "=v"(p_c_thread[3]),
                           "=v"(p_c_thread[4]),
                           "=v"(p_c_thread[5]),
                           "=v"(p_c_thread[6]),
                           "=v"(p_c_thread[7]),
                           "=v"(p_c_thread[8]),
                           "=v"(p_c_thread[9]),
                           "=v"(p_c_thread[10]),
                           "=v"(p_c_thread[11]),
                           "=v"(p_c_thread[12]),
                           "=v"(p_c_thread[13]),
                           "=v"(p_c_thread[14]),
                           "=v"(p_c_thread[15]),
                           "=v"(p_c_thread[16]),
                           "=v"(p_c_thread[17]),
                           "=v"(p_c_thread[18]),
                           "=v"(p_c_thread[19]),
                           "=v"(p_c_thread[20]),
                           "=v"(p_c_thread[21]),
                           "=v"(p_c_thread[22]),
                           "=v"(p_c_thread[23]),
                           "=v"(p_c_thread[24]),
                           "=v"(p_c_thread[25]),
                           "=v"(p_c_thread[26]),
                           "=v"(p_c_thread[27]),
                           "=v"(p_c_thread[28]),
                           "=v"(p_c_thread[29]),
                           "=v"(p_c_thread[30]),
                           "=v"(p_c_thread[31]),
                           "=v"(p_c_thread[32]),
                           "=v"(p_c_thread[33]),
                           "=v"(p_c_thread[34]),
                           "=v"(p_c_thread[35]),
                           "=v"(p_c_thread[36]),
                           "=v"(p_c_thread[37]),
                           "=v"(p_c_thread[38]),
                           "=v"(p_c_thread[39]),
                           "=v"(p_c_thread[40]),
                           "=v"(p_c_thread[41]),
                           "=v"(p_c_thread[42]),
                           "=v"(p_c_thread[43]),
                           "=v"(p_c_thread[44]),
                           "=v"(p_c_thread[45]),
                           "=v"(p_c_thread[46]),
                           "=v"(p_c_thread[47]),
                           "=v"(p_c_thread[48]),
                           "=v"(p_c_thread[49]),
                           "=v"(p_c_thread[50]),
                           "=v"(p_c_thread[51]),
                           "=v"(p_c_thread[52]),
                           "=v"(p_c_thread[53]),
                           "=v"(p_c_thread[54]),
                           "=v"(p_c_thread[55]),
                           "=v"(p_c_thread[56]),
                           "=v"(p_c_thread[57]),
                           "=v"(p_c_thread[58]),
                           "=v"(p_c_thread[59]),
                           "=v"(p_c_thread[60]),
                           "=v"(p_c_thread[61]),
                           "=v"(p_c_thread[62]),
                           "=v"(p_c_thread[63])
                         : "v"(p_a_thread[0]),
                           "v"(p_a_thread[1]),
                           "v"(p_a_thread[2]),
                           "v"(p_a_thread[3]),
                           "v"(p_a_thread[4]),
                           "v"(p_a_thread[5]),
                           "v"(p_a_thread[6]),
                           "v"(p_a_thread[7]),
                           "v"(p_b_thread[0]),
                           "v"(p_b_thread[1]),
                           "v"(p_b_thread[2]),
                           "v"(p_b_thread[3]),
                           "v"(p_b_thread[4]),
                           "v"(p_b_thread[5]),
                           "v"(p_b_thread[6]),
                           "v"(p_b_thread[7]),
                           "v"(__to_local((void*)(a_loc))),
                           "v"(__to_local((void*)(b_loc))),
                           "4"(p_c_thread[0]),
                           "5"(p_c_thread[1]),
                           "6"(p_c_thread[2]),
                           "7"(p_c_thread[3]),
                           "8"(p_c_thread[4]),
                           "9"(p_c_thread[5]),
                           "10"(p_c_thread[6]),
                           "11"(p_c_thread[7]),
                           "12"(p_c_thread[8]),
                           "13"(p_c_thread[9]),
                           "14"(p_c_thread[10]),
                           "15"(p_c_thread[11]),
                           "16"(p_c_thread[12]),
                           "17"(p_c_thread[13]),
                           "18"(p_c_thread[14]),
                           "19"(p_c_thread[15]),
                           "20"(p_c_thread[16]),
                           "21"(p_c_thread[17]),
                           "22"(p_c_thread[18]),
                           "23"(p_c_thread[19]),
                           "24"(p_c_thread[20]),
                           "25"(p_c_thread[21]),
                           "26"(p_c_thread[22]),
                           "27"(p_c_thread[23]),
                           "28"(p_c_thread[24]),
                           "29"(p_c_thread[25]),
                           "30"(p_c_thread[26]),
                           "31"(p_c_thread[27]),
                           "32"(p_c_thread[28]),
                           "33"(p_c_thread[29]),
                           "34"(p_c_thread[30]),
                           "35"(p_c_thread[31]),
                           "36"(p_c_thread[32]),
                           "37"(p_c_thread[33]),
                           "38"(p_c_thread[34]),
                           "39"(p_c_thread[35]),
                           "40"(p_c_thread[36]),
                           "41"(p_c_thread[37]),
                           "42"(p_c_thread[38]),
                           "43"(p_c_thread[39]),
                           "44"(p_c_thread[40]),
                           "45"(p_c_thread[41]),
                           "46"(p_c_thread[42]),
                           "47"(p_c_thread[43]),
                           "48"(p_c_thread[44]),
                           "49"(p_c_thread[45]),
                           "50"(p_c_thread[46]),
                           "51"(p_c_thread[47]),
                           "52"(p_c_thread[48]),
                           "53"(p_c_thread[49]),
                           "54"(p_c_thread[50]),
                           "55"(p_c_thread[51]),
                           "56"(p_c_thread[52]),
                           "57"(p_c_thread[53]),
                           "58"(p_c_thread[54]),
                           "59"(p_c_thread[55]),
                           "60"(p_c_thread[56]),
                           "61"(p_c_thread[57]),
                           "62"(p_c_thread[58]),
                           "63"(p_c_thread[59]),
                           "64"(p_c_thread[60]),
                           "65"(p_c_thread[61]),
                           "66"(p_c_thread[62]),
                           "67"(p_c_thread[63]));
#endif
        }
#else
        printf("asm only support on HIP backend\n");
        assert(false);
#endif
    }

    template <class FloatA, class FloatB, class FloatC, class Accumulator>
    __device__ void Run(const FloatA* const __restrict__ p_a_block,
                        const FloatB* const __restrict__ p_b_block,
                        FloatC* const __restrict__ p_c_thread,
                        Accumulator f_accum) const
    {
        constexpr auto True  = integral_constant<bool, true>{};
        constexpr auto False = integral_constant<bool, false>{};

        constexpr auto a_block_mtx  = BlockMatrixA{};
        constexpr auto b_block_mtx  = BlockMatrixB{};
        constexpr auto c_thread_mtx = ThreadMatrixC{};

        constexpr index_t M = a_block_mtx.NCol();
        constexpr index_t N = b_block_mtx.NCol();
        constexpr index_t K = a_block_mtx.NRow();

        constexpr index_t MPerThread = c_thread_mtx.NRow();
        constexpr index_t NPerThread = c_thread_mtx.NCol();

        // thread A, B for GEMM
        constexpr auto a_thread_mtx =
            make_ConstantMatrixDescriptor(Number<KPerThreadLoop>{}, Number<MPerThread>{});

        constexpr auto b_thread_mtx =
            make_ConstantMatrixDescriptor(Number<KPerThreadLoop>{}, Number<NPerThread>{});

        // thread A-sub, B-sub for copy
        constexpr auto a_thread_sub_mtx = make_ConstantMatrixDescriptor(
            Number<KPerThreadLoop>{}, Number<MPerThreadSubC>{}, Number<MPerThread>{});

        constexpr auto b_thread_sub_mtx = make_ConstantMatrixDescriptor(
            Number<KPerThreadLoop>{}, Number<NPerThreadSubC>{}, Number<NPerThread>{});

        FloatA p_a_thread[a_thread_mtx.GetElementSpace()];
        FloatB p_b_thread[b_thread_mtx.GetElementSpace()];

        constexpr index_t MPerLevel1Cluster = MPerThreadSubC * MLevel0Cluster * MLevel1Cluster;
        constexpr index_t NPerLevel1Cluster = NPerThreadSubC * NLevel0Cluster * NLevel1Cluster;

        constexpr index_t MRepeat = MPerThread / MPerThreadSubC;
        constexpr index_t NRepeat = NPerThread / NPerThreadSubC;

        const FloatA* const p_a_block_thread_offset = p_a_block + mMyThreadOffsetA;

#pragma unroll
        // loop over k
        for(index_t k_begin = 0; k_begin < K; k_begin += KPerThreadLoop)
        {
#pragma unroll
            // copy A-sub to form A
            for(index_t m_repeat = 0; m_repeat < MRepeat; ++m_repeat)
            {
                threadwise_matrix_copy(
                    a_block_mtx,
                    p_a_block + a_block_mtx.Get1dIndex(k_begin, m_repeat * MPerLevel1Cluster) +
                        mMyThreadOffsetA,
                    a_thread_mtx,
                    p_a_thread + a_thread_mtx.Get1dIndex(0, m_repeat * MPerThreadSubC),
                    a_thread_sub_mtx.GetLengths());
            }

#pragma unroll
            // copy B-sub to form B
            for(index_t n_repeat = 0; n_repeat < NRepeat; ++n_repeat)
            {
                threadwise_matrix_copy(
                    b_block_mtx,
                    p_b_block + b_block_mtx.Get1dIndex(k_begin, n_repeat * NPerLevel1Cluster) +
                        mMyThreadOffsetB,
                    b_thread_mtx,
                    p_b_thread + b_thread_mtx.Get1dIndex(0, n_repeat * NPerThreadSubC),
                    b_thread_sub_mtx.GetLengths());
            }

            // C = A * B
            threadwise_gemm(a_thread_mtx,
                            True,
                            p_a_thread,
                            b_thread_mtx,
                            False,
                            p_b_thread,
                            c_thread_mtx,
                            False,
                            p_c_thread,
                            f_accum);
        }
    }

    template <class FloatA, class FloatB, class FloatC, class Accumulator>
    __device__ void Run_RegisterDoubleBuffer(FloatA* const p_a_block,
                                             FloatB* const p_b_block,
                                             FloatC* p_c_thread,
                                             Accumulator f_accum) const
    {
        constexpr auto True  = integral_constant<bool, true>{};
        constexpr auto False = integral_constant<bool, false>{};

        constexpr auto a_block_mtx  = BlockMatrixA{};
        constexpr auto b_block_mtx  = BlockMatrixB{};
        constexpr auto c_thread_mtx = ThreadMatrixC{};

        constexpr index_t M = a_block_mtx.NCol();
        constexpr index_t N = b_block_mtx.NCol();
        constexpr index_t K = a_block_mtx.NRow();

        constexpr index_t MPerThread = c_thread_mtx.NRow();
        constexpr index_t NPerThread = c_thread_mtx.NCol();

        // thread A, B for GEMM
        constexpr auto a_thread_mtx =
            make_ConstantMatrixDescriptor(Number<KPerThreadLoop>{}, Number<MPerThread>{});

        constexpr auto b_thread_mtx =
            make_ConstantMatrixDescriptor(Number<KPerThreadLoop>{}, Number<NPerThread>{});

        // thread A-sub, B-sub for copy
        constexpr auto a_thread_sub_mtx = make_ConstantMatrixDescriptor(
            Number<KPerThreadLoop>{}, Number<MPerThreadSubC>{}, Number<MPerThread>{});

        constexpr auto b_thread_sub_mtx = make_ConstantMatrixDescriptor(
            Number<KPerThreadLoop>{}, Number<NPerThreadSubC>{}, Number<NPerThread>{});

        // register
        FloatA p_a_thread_0[a_thread_mtx.GetElementSpace()];
        FloatB p_b_thread_0[b_thread_mtx.GetElementSpace()];

        FloatA p_a_thread_1[a_thread_mtx.GetElementSpace()];
        FloatB p_b_thread_1[b_thread_mtx.GetElementSpace()];

        constexpr index_t MPerLevel1Cluster = MPerThreadSubC * MLevel0Cluster * MLevel1Cluster;
        constexpr index_t NPerLevel1Cluster = NPerThreadSubC * NLevel0Cluster * NLevel1Cluster;

        constexpr index_t MRepeat = MPerThread / MPerThreadSubC;
        constexpr index_t NRepeat = NPerThread / NPerThreadSubC;

// preload A, B
#pragma unroll
        for(index_t m_repeat = 0; m_repeat < MRepeat; ++m_repeat)
        { // copy A-sub to form A
            threadwise_matrix_copy(a_block_mtx,
                                   p_a_block + mMyThreadOffsetA + m_repeat * MPerLevel1Cluster,
                                   a_thread_sub_mtx,
                                   p_a_thread_0 + m_repeat * MPerThreadSubC,
                                   a_thread_sub_mtx.GetLengths());
        }

#pragma unroll
        for(index_t n_repeat = 0; n_repeat < NRepeat; ++n_repeat)
        { // copy B-sub to form B
            threadwise_matrix_copy(b_block_mtx,
                                   p_b_block + mMyThreadOffsetB + n_repeat * NPerLevel1Cluster,
                                   b_thread_sub_mtx,
                                   p_b_thread_0 + n_repeat * NPerThreadSubC,
                                   b_thread_sub_mtx.GetLengths());
        }

        bool even_loop = true;

#pragma unroll
        for(index_t k_begin = 0; k_begin + KPerThreadLoop < K;
            k_begin += KPerThreadLoop, even_loop = !even_loop)
        { // loop over k
            FloatA* p_a_thread_now = even_loop ? p_a_thread_0 : p_a_thread_1;
            FloatB* p_b_thread_now = even_loop ? p_b_thread_0 : p_b_thread_1;

            FloatA* p_a_thread_next = even_loop ? p_a_thread_1 : p_a_thread_0;
            FloatB* p_b_thread_next = even_loop ? p_b_thread_1 : p_b_thread_0;

// preload next A, B
#pragma unroll
            for(index_t m_repeat = 0; m_repeat < MRepeat; ++m_repeat)
            { // copy A-sub to form A
                threadwise_matrix_copy(a_block_mtx,
                                       p_a_block + mMyThreadOffsetA +
                                           (k_begin + 1) * a_block_mtx.RowStride() +
                                           m_repeat * MPerLevel1Cluster,
                                       a_thread_sub_mtx,
                                       p_a_thread_next + m_repeat * MPerThreadSubC,
                                       a_thread_sub_mtx.GetLengths());
            }

#pragma unroll
            for(index_t n_repeat = 0; n_repeat < NRepeat; ++n_repeat)
            { // copy B-sub to form B
                threadwise_matrix_copy(b_block_mtx,
                                       p_b_block + mMyThreadOffsetB +
                                           (k_begin + 1) * b_block_mtx.RowStride() +
                                           n_repeat * NPerLevel1Cluster,
                                       b_thread_sub_mtx,
                                       p_b_thread_next + n_repeat * NPerThreadSubC,
                                       b_thread_sub_mtx.GetLengths());
            }

            // C = A * B
            threadwise_gemm(a_thread_mtx,
                            True,
                            p_a_thread_now,
                            b_thread_mtx,
                            False,
                            p_b_thread_now,
                            c_thread_mtx,
                            False,
                            p_c_thread,
                            f_accum);
        }

        // last loop
        {
            FloatA* p_a_thread_now = even_loop ? p_a_thread_0 : p_a_thread_1;
            FloatB* p_b_thread_now = even_loop ? p_b_thread_0 : p_b_thread_1;

            // C = A * B
            threadwise_gemm(a_thread_mtx,
                            True,
                            p_a_thread_now,
                            b_thread_mtx,
                            False,
                            p_b_thread_now,
                            c_thread_mtx,
                            False,
                            p_c_thread,
                            f_accum);
        }
    }

    template <class FloatA, class FloatB, class FloatC, class Accumulator>
    __device__ void Run_PipelineReadAndCompute(const FloatA* __restrict__ p_a_block,
                                               const FloatB* __restrict__ p_b_block,
                                               FloatC* __restrict__ p_c_thread,
                                               Accumulator f_accum) const
    {
        constexpr auto True  = integral_constant<bool, true>{};
        constexpr auto False = integral_constant<bool, false>{};

        constexpr auto a_block_mtx  = BlockMatrixA{};
        constexpr auto b_block_mtx  = BlockMatrixB{};
        constexpr auto c_thread_mtx = ThreadMatrixC{};

        constexpr index_t M = a_block_mtx.NCol();
        constexpr index_t N = b_block_mtx.NCol();
        constexpr index_t K = a_block_mtx.NRow();

        constexpr index_t MPerThread = c_thread_mtx.NRow();
        constexpr index_t NPerThread = c_thread_mtx.NCol();

        // thread A-sub, B-sub, C-sub
        constexpr auto a_thread_sub_mtx = make_ConstantMatrixDescriptor(
            Number<KPerThreadLoop>{}, Number<MPerThreadSubC>{}, Number<MPerThread>{});

        constexpr auto b_thread_sub_mtx = make_ConstantMatrixDescriptor(
            Number<KPerThreadLoop>{}, Number<NPerThreadSubC>{}, Number<NPerThread>{});

        constexpr auto c_thread_sub_mtx = make_ConstantMatrixDescriptor(
            Number<MPerThreadSubC>{}, Number<NPerThreadSubC>{}, Number<NPerThread>{});

        // thread A, B
        constexpr auto a_thread_mtx =
            make_ConstantMatrixDescriptor(Number<KPerThreadLoop>{}, Number<MPerThread>{});

        constexpr auto b_thread_mtx =
            make_ConstantMatrixDescriptor(Number<KPerThreadLoop>{}, Number<NPerThread>{});

        FloatA p_a_thread[a_thread_mtx.GetElementSpace()];
        FloatB p_b_thread[b_thread_mtx.GetElementSpace()];

        constexpr index_t MPerLevel1Cluster = MPerThreadSubC * MLevel0Cluster * MLevel1Cluster;
        constexpr index_t NPerLevel1Cluster = NPerThreadSubC * NLevel0Cluster * NLevel1Cluster;

        constexpr index_t MRepeat = MPerThread / MPerThreadSubC;
        constexpr index_t NRepeat = NPerThread / NPerThreadSubC;

#pragma unroll
        // loop over k
        for(index_t k_begin = 0; k_begin < K; k_begin += KPerThreadLoop)
        {
            // C-sub(s) in first row-wise subblock of C
            {
                //   copy first A-sub
                threadwise_matrix_copy(a_block_mtx,
                                       p_a_block + a_block_mtx.Get1dIndex(k_begin, 0) +
                                           mMyThreadOffsetA,
                                       a_thread_mtx,
                                       p_a_thread,
                                       a_thread_sub_mtx.GetLengths());

                //   copy first B-sub
                threadwise_matrix_copy(b_block_mtx,
                                       p_b_block + b_block_mtx.Get1dIndex(k_begin, 0) +
                                           mMyThreadOffsetB,
                                       b_thread_mtx,
                                       p_b_thread,
                                       b_thread_sub_mtx.GetLengths());

                //   do first sub GEMM
                threadwise_gemm(a_thread_sub_mtx,
                                True,
                                p_a_thread,
                                b_thread_sub_mtx,
                                False,
                                p_b_thread,
                                c_thread_sub_mtx,
                                False,
                                p_c_thread,
                                f_accum);

#pragma unroll
                //   copy next B-sub, and do GEMM
                for(index_t n_repeat = 1; n_repeat < NRepeat; ++n_repeat)
                {
                    threadwise_matrix_copy(
                        b_block_mtx,
                        p_b_block + b_block_mtx.Get1dIndex(k_begin, n_repeat * NPerLevel1Cluster) +
                            mMyThreadOffsetB,
                        b_thread_mtx,
                        p_b_thread + b_thread_mtx.Get1dIndex(0, n_repeat * NPerThreadSubC),
                        b_thread_sub_mtx.GetLengths());

                    threadwise_gemm(
                        a_thread_sub_mtx,
                        True,
                        p_a_thread,
                        b_thread_sub_mtx,
                        False,
                        p_b_thread + b_thread_mtx.Get1dIndex(0, n_repeat * NPerThreadSubC),
                        c_thread_sub_mtx,
                        False,
                        p_c_thread + c_thread_mtx.Get1dIndex(0, n_repeat * NPerThreadSubC),
                        f_accum);
                }

#pragma unroll
                // loop over rest of row-wise subblock
                //   all B-sub(s) has been copied, so only A-sub(s) need to be copied
                for(index_t m_repeat = 1; m_repeat < MRepeat; ++m_repeat)
                {
                    // copy a A-sub
                    threadwise_matrix_copy(
                        a_block_mtx,
                        p_a_block + a_block_mtx.Get1dIndex(k_begin, m_repeat * MPerLevel1Cluster) +
                            mMyThreadOffsetA,
                        a_thread_mtx,
                        p_a_thread + a_thread_mtx.Get1dIndex(0, m_repeat * MPerThreadSubC),
                        a_thread_sub_mtx.GetLengths());

                    // do some GEMMs
                    for(index_t n_repeat = 0; n_repeat < NRepeat; ++n_repeat)
                    {
                        threadwise_gemm(
                            a_thread_sub_mtx,
                            True,
                            p_a_thread + a_thread_mtx.Get1dIndex(0, m_repeat * MPerThreadSubC),
                            b_thread_sub_mtx,
                            False,
                            p_b_thread + b_thread_mtx.Get1dIndex(0, n_repeat * NPerThreadSubC),
                            c_thread_sub_mtx,
                            False,
                            p_c_thread +
                                c_thread_mtx.Get1dIndex(m_repeat * MPerThreadSubC,
                                                        n_repeat * NPerThreadSubC),
                            f_accum);
                    }
                }
            }
        }
    }
};
