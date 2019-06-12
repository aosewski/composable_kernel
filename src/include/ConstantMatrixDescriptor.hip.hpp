#pragma once
#include "common.hip.hpp"

template <index_t NRow_, index_t NCol_, index_t RowStride_>
struct ConstantMatrixDescriptor
{
    __host__ __device__ constexpr ConstantMatrixDescriptor()
    {
        static_assert(NCol_ <= RowStride_, "wrong! NCol > RowStride!");
    }

    __host__ __device__ static constexpr index_t NRow() { return NRow_; }

    __host__ __device__ static constexpr index_t NCol() { return NCol_; }

    __host__ __device__ static constexpr index_t RowStride() { return RowStride_; }

    __host__ __device__ static constexpr auto GetLengths() { return Sequence<NRow_, NCol_>{}; }

    __host__ __device__ static constexpr index_t GetElementSize() { return NRow_ * NCol_; }

    __host__ __device__ static constexpr index_t GetElementSpace() { return NRow_ * RowStride_; }

    __host__ __device__ static index_t GetOffsetFromMultiIndex(index_t irow, index_t icol)
    {
        return irow * RowStride_ + icol;
    }

    template <index_t SubNRow, index_t SubNCol>
    __host__ __device__ static constexpr auto MakeSubMatrixDescriptor(Number<SubNRow>,
                                                                      Number<SubNCol>)
    {
        return ConstantMatrixDescriptor<SubNRow, SubNCol, RowStride_>{};
    }
};

template <index_t NRow, index_t NCol>
__host__ __device__ constexpr auto make_ConstantMatrixDescriptor(Number<NRow>, Number<NCol>)
{
    return ConstantMatrixDescriptor<NRow, NCol, NCol>{};
}

template <index_t NRow, index_t NCol, index_t RowStride>
__host__ __device__ constexpr auto
    make_ConstantMatrixDescriptor(Number<NRow>, Number<NCol>, Number<RowStride>)
{
    return ConstantMatrixDescriptor<NRow, NCol, RowStride>{};
}

template <class TDesc>
__host__ __device__ void print_ConstantMatrixDescriptor(TDesc, const char* s)
{
    const auto desc = TDesc{};

    constexpr auto I0 = Number<0>{};
    constexpr auto I1 = Number<1>{};

    printf("%s NRow %u NCol %u RowStride %u\n", s, desc.NRow(), desc.NCol(), desc.RowStride());
}