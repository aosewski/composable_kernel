#ifndef CK_THREADWISE_TENSOR_SLICE_TRANSFER_V6R2_HPP
#define CK_THREADWISE_TENSOR_SLICE_TRANSFER_V6R2_HPP

#include "common_header.hpp"
#include "tensor_descriptor.hpp"
#include "tensor_descriptor_helper.hpp"

namespace ck {

// Do following things to avoid "alloca" in LLVM-IR, which would cause scratch memory
// and sometimes useless instructions:
//   1. Don't save a reference to tensor descriptor in class, pass in tensor descriptor as argument
//   instead
//   2. Don't construct a new tensor coordinate everytime when using it, update and reuse the same
//   tensor coordinate instead
//   3. Don't use a pointer to VGPR buffer, use vector instead

// Assume:
//   1. src0_desc and dst_desc are not known at compile-time
//   2. SrcBuffer and DstBuffer are DynamicBuffer
//   3. src_slice_origin and dst_slice_origin are not known at compile-time,
template <typename Src0Data,
          typename Src1Data,
          typename DstData,
          typename Src0Desc,
          typename Src1Desc,
          typename DstDesc,
          typename ElementwiseOperation,
          typename SliceLengths,
          typename DimAccessOrder,
          index_t VectorDim,
          index_t ScalarPerVector,
          InMemoryDataOperationEnum_t DstInMemOp,
          bool Src0ResetCoordinateAfterRun,
          bool Src1ResetCoordinateAfterRun,
          bool DstResetCoordinateAfterRun>
struct ThreadwiseTensorSliceTransfer_v6r2
{
    static constexpr index_t nDim = SliceLengths::Size();

    using Index = MultiIndex<nDim>;

    using Src0Coord = decltype(make_tensor_coordinate(Src0Desc{}, Index{}));
    using Src1Coord = decltype(make_tensor_coordinate(Src1Desc{}, Index{}));
    using DstCoord  = decltype(make_tensor_coordinate(DstDesc{}, Index{}));

    using Src0CoordStep = decltype(make_tensor_coordinate_step(Src0Desc{}, Index{}));
    using Src1CoordStep = decltype(make_tensor_coordinate_step(Src1Desc{}, Index{}));
    using DstCoordStep  = decltype(make_tensor_coordinate_step(DstDesc{}, Index{}));

    static constexpr auto I0 = Number<0>{};

    __device__ constexpr ThreadwiseTensorSliceTransfer_v6r2(const Src0Desc& src0_desc,
                                                            const Index& src0_slice_origin,
                                                            const Src1Desc& src1_desc,
                                                            const Index& src1_slice_origin,
                                                            const DstDesc& dst_desc,
                                                            const Index& dst_slice_origin,
                                                            const ElementwiseOperation& element_op)
        : src0_coord_(make_tensor_coordinate(src0_desc, src0_slice_origin)),
          src1_coord_(make_tensor_coordinate(src1_desc, src1_slice_origin)),
          dst_coord_(make_tensor_coordinate(dst_desc, dst_slice_origin)),
          element_op_(element_op)
    {
        static_assert(SliceLengths::At(Number<VectorDim>{}) % ScalarPerVector == 0,
                      "wrong! cannot evenly divide");
    }

    __device__ void SetSrc0SliceOrigin(const Src0Desc& src0_desc,
                                       const Index& src0_slice_origin_idx)
    {
        src0_coord_ = make_tensor_coordinate(src0_desc, src0_slice_origin_idx);
    }

    __device__ void SetSrc1SliceOrigin(const Src1Desc& src1_desc,
                                       const Index& src1_slice_origin_idx)
    {
        src1_coord_ = make_tensor_coordinate(src1_desc, src1_slice_origin_idx);
    }

    __device__ void SetDstSliceOrigin(const DstDesc& dst_desc, const Index& dst_slice_origin_idx)
    {
        dst_coord_ = make_tensor_coordinate(dst_desc, dst_slice_origin_idx);
    }

    template <typename Src0Buffer, typename Src1Buffer, typename DstBuffer>
    __device__ void Run(const Src0Desc& src0_desc,
                        const Src0Buffer& src0_buf,
                        const Src1Desc& src1_desc,
                        const Src1Buffer& src1_buf,
                        const DstDesc& dst_desc,
                        DstBuffer& dst_buf)
    {
        // scalar per access on each dim
        // TODO: don't use lambda_scalar_per_access
        constexpr auto scalar_per_access = generate_sequence(
            detail::lambda_scalar_per_access<VectorDim, ScalarPerVector>{}, Number<nDim>{});

        constexpr auto access_lengths = SliceLengths{} / scalar_per_access;

        constexpr auto dim_access_order = DimAccessOrder{};

        constexpr auto ordered_access_lengths =
            container_reorder_given_new2old(access_lengths, dim_access_order);

        auto make_forward_steps = [&](auto desc) {
            return generate_tuple(
                [&](auto i) {
                    Index forward_step_idx;

                    static_for<0, nDim, 1>{}([&](auto j) {
                        forward_step_idx(j) = (i.value == j.value) ? scalar_per_access[i] : 0;
                    });

                    return make_tensor_coordinate_step(desc, forward_step_idx);
                },
                Number<nDim>{});
        };

        auto make_backward_steps = [&](auto desc) {
            return generate_tuple(
                [&](auto i) {
                    Index backward_step_idx;

                    static_for<0, nDim, 1>{}([&](auto j) {
                        backward_step_idx(j) = (i.value == j.value) ? -scalar_per_access[i] : 0;
                    });

                    return make_tensor_coordinate_step(desc, backward_step_idx);
                },
                Number<nDim>{});
        };

        // make forward steps
        const auto src0_forward_steps = make_forward_steps(src0_desc);
        const auto src1_forward_steps = make_forward_steps(src1_desc);
        const auto dst_forward_steps  = make_forward_steps(dst_desc);

        // make backward steps
        const auto src0_backward_steps = make_backward_steps(src0_desc);
        const auto src1_backward_steps = make_backward_steps(src1_desc);
        const auto dst_backward_steps  = make_backward_steps(dst_desc);

        // loop over slice window
        static_ford<decltype(ordered_access_lengths)>{}([&](auto ordered_access_idx) {
            // judge move forward or move backward
            constexpr auto forward_sweep = [&]() {
                StaticallyIndexedArray<bool, nDim> forward_sweep_;

                forward_sweep_(I0) = true;

                static_for<1, nDim, 1>{}([&](auto i) {
                    index_t tmp = ordered_access_idx[I0];

                    static_for<1, i, 1>{}([&](auto j) {
                        tmp = tmp * ordered_access_lengths[j] + ordered_access_idx[j];
                    });

                    forward_sweep_(i) = tmp % 2 == 0;
                });

                return forward_sweep_;
            }();

            using src0_vector_type = vector_type_maker_t<Src0Data, ScalarPerVector>;
            using src0_vector_t    = typename src0_vector_type::type;

            using src1_vector_type = vector_type_maker_t<Src1Data, ScalarPerVector>;
            using src1_vector_t    = typename src1_vector_type::type;

            using dst_vector_type = vector_type_maker_t<DstData, ScalarPerVector>;
            using dst_vector_t    = typename dst_vector_type::type;

            const bool is_src0_valid =
                coordinate_has_valid_offset_assuming_visible_index_is_valid(src0_desc, src0_coord_);

            const bool is_src1_valid =
                coordinate_has_valid_offset_assuming_visible_index_is_valid(src1_desc, src1_coord_);

            // copy data from src0_buf into src0_vector_container
            auto src0_vector_container = src0_vector_type{
                src0_buf.template Get<src0_vector_t>(src0_coord_.GetOffset(), is_src0_valid)};

            auto src1_vector_container = src1_vector_type{
                src1_buf.template Get<src1_vector_t>(src1_coord_.GetOffset(), is_src1_valid)};

            auto dst_vector_container = dst_vector_type{};

            // apply pointwise operation
            static_for<0, ScalarPerVector, 1>{}([&](auto i) {
                element_op_(dst_vector_container.template AsType<DstData>()(i),
                            src0_vector_container.template AsType<Src0Data>()[i],
                            src1_vector_container.template AsType<Src1Data>()[i]);
            });

            const bool is_dst_valid =
                coordinate_has_valid_offset_assuming_visible_index_is_valid(dst_desc, dst_coord_);

            // copy data from dst_vector into dst_buf
            if constexpr(DstInMemOp == InMemoryDataOperationEnum_t::Set)
            {
                dst_buf.template Set<dst_vector_t>(
                    dst_coord_.GetOffset(),
                    is_dst_valid,
                    dst_vector_container.template AsType<dst_vector_t>()[I0]);
            }
            else if constexpr(DstInMemOp == InMemoryDataOperationEnum_t::AtomicAdd)
            {
                dst_buf.template AtomicAdd<dst_vector_t>(
                    dst_coord_.GetOffset(),
                    is_dst_valid,
                    dst_vector_container.template AsType<dst_vector_t>()[I0]);
            }

            constexpr auto move_on_dim = [&]() constexpr
            {
                StaticallyIndexedArray<bool, nDim> move_on_dim_;

                static_for<0, nDim, 1>{}([&](auto i) {
                    move_on_dim_(i) = ordered_access_idx[i] < ordered_access_lengths[i] - 1;

                    static_for<i + 1, nDim, 1>{}([&](auto j) {
                        move_on_dim_(i) &= ordered_access_idx[j] == ordered_access_lengths[j] - 1;
                    });
                });

                return move_on_dim_;
            }
            ();

            // move coordinate
            static_for<0, nDim, 1>{}([&](auto i) {
                if constexpr(move_on_dim[i])
                {
                    if constexpr(forward_sweep[i])
                    {
                        move_tensor_coordinate(
                            src0_desc, src0_coord_, src0_forward_steps[dim_access_order[i]]);

                        move_tensor_coordinate(
                            src1_desc, src1_coord_, src1_forward_steps[dim_access_order[i]]);

                        move_tensor_coordinate(
                            dst_desc, dst_coord_, dst_forward_steps[dim_access_order[i]]);
                    }
                    else
                    {
                        move_tensor_coordinate(
                            src0_desc, src0_coord_, src0_backward_steps[dim_access_order[i]]);

                        move_tensor_coordinate(
                            src1_desc, src1_coord_, src1_backward_steps[dim_access_order[i]]);

                        move_tensor_coordinate(
                            dst_desc, dst_coord_, dst_backward_steps[dim_access_order[i]]);
                    }
                }
            });
        });

        // move coordinate back to slice origin (or not)
        if constexpr(Src0ResetCoordinateAfterRun)
        {
            const auto src0_reset_step =
                make_tensor_coordinate_step(src0_desc, GetCoordinateResetStep());

            move_tensor_coordinate(src0_desc, src0_coord_, src0_reset_step);
        }

        if constexpr(Src1ResetCoordinateAfterRun)
        {
            const auto src1_reset_step =
                make_tensor_coordinate_step(src1_desc, GetCoordinateResetStep());

            move_tensor_coordinate(src1_desc, src1_coord_, src1_reset_step);
        }

        if constexpr(DstResetCoordinateAfterRun)
        {
            const auto dst_reset_step =
                make_tensor_coordinate_step(dst_desc, GetCoordinateResetStep());

            move_tensor_coordinate(dst_desc, dst_coord_, dst_reset_step);
        }
    }

    __device__ static constexpr auto GetCoordinateResetStep()
    {
        // scalar per access on each dim
        // TODO: don't use lambda_scalar_per_access
        constexpr auto scalar_per_access = generate_sequence(
            detail::lambda_scalar_per_access<VectorDim, ScalarPerVector>{}, Number<nDim>{});

        constexpr auto access_lengths = SliceLengths{} / scalar_per_access;

        constexpr auto dim_access_order = DimAccessOrder{};

        constexpr auto ordered_access_lengths =
            container_reorder_given_new2old(access_lengths, dim_access_order);

        // judge move forward or move backward during the last iteration
        constexpr auto forward_sweep = [&]() {
            StaticallyIndexedArray<bool, nDim> forward_sweep_;

            forward_sweep_(I0) = true;

            static_for<1, nDim, 1>{}([&](auto i) {
                index_t tmp = ordered_access_lengths[I0] - 1;

                static_for<1, i, 1>{}([&](auto j) {
                    tmp = tmp * ordered_access_lengths[j] + ordered_access_lengths[j] - 1;
                });

                forward_sweep_(i) = tmp % 2 == 0;
            });

            return forward_sweep_;
        }();

        // calculate data index after last iteration in Run(), if it has not being reset
        constexpr auto data_idx = [&]() {
            Index ordered_idx;

            static_for<0, nDim, 1>{}([&](auto i) {
                ordered_idx(i) = forward_sweep[i] ? ordered_access_lengths[i] - 1 : 0;
            });

            return container_reorder_given_old2new(ordered_idx, dim_access_order) *
                   scalar_per_access;
        }();

        //
        constexpr auto reset_data_step = [&]() {
            Index reset_data_step_;

            static_for<0, nDim, 1>{}([&](auto i) { reset_data_step_(i) = -data_idx[i]; });

            return reset_data_step_;
        }();

        return reset_data_step;
    }

    // src_slice_origin_step_idx need to be known at compile-time, for performance reason
    __device__ void MoveSrc0SliceWindow(const Src0Desc& src0_desc,
                                        const Index& src0_slice_origin_step_idx)
    {
        // if src coord was not reset by RunRead(), then need to adjust the step here
        const auto adjusted_step_idx = Src0ResetCoordinateAfterRun
                                           ? src0_slice_origin_step_idx
                                           : src0_slice_origin_step_idx + GetCoordinateResetStep();

        // is it OK to construct a new step every time?
        const auto adjusted_step = make_tensor_coordinate_step(src0_desc, adjusted_step_idx);

        move_tensor_coordinate(src0_desc, src0_coord_, adjusted_step);
    }

    // src_slice_origin_step_idx need to be known at compile-time, for performance reason
    __device__ void MoveSrc1SliceWindow(const Src1Desc& src1_desc,
                                        const Index& src1_slice_origin_step_idx)
    {
        // if src coord was not reset by RunRead(), then need to adjust the step here
        const auto adjusted_step_idx = Src1ResetCoordinateAfterRun
                                           ? src1_slice_origin_step_idx
                                           : src1_slice_origin_step_idx + GetCoordinateResetStep();

        // is it OK to construct a new step every time?
        const auto adjusted_step = make_tensor_coordinate_step(src1_desc, adjusted_step_idx);

        move_tensor_coordinate(src1_desc, src1_coord_, adjusted_step);
    }

    // dst_slice_origin_step_idx need to be known at compile-time, for performance reason
    __device__ void MoveDstSliceWindow(const DstDesc& dst_desc,
                                       const Index& dst_slice_origin_step_idx)
    {
        // if dst coord was not reset by Run(), then need to adjust the step here
        const auto adjusted_step_idx = DstResetCoordinateAfterRun
                                           ? dst_slice_origin_step_idx
                                           : dst_slice_origin_step_idx + GetCoordinateResetStep();

        // is it OK to construct a new step every time?
        const auto adjusted_step = make_tensor_coordinate_step(dst_desc, adjusted_step_idx);

        move_tensor_coordinate(dst_desc, dst_coord_, adjusted_step);
    }

    private:
    Src0Coord src0_coord_;
    Src1Coord src1_coord_;
    DstCoord dst_coord_;
    const ElementwiseOperation element_op_;
};

} // namespace ck
#endif
