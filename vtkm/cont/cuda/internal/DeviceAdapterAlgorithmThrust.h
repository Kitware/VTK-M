
//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//
//  Copyright 2014 Sandia Corporation.
//  Copyright 2014 UT-Battelle, LLC.
//  Copyright 2014 Los Alamos National Security.
//
//  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
//  the U.S. Government retains certain rights in this software.
//
//  Under the terms of Contract DE-AC52-06NA25396 with Los Alamos National
//  Laboratory (LANL), the U.S. Government retains certain rights in
//  this software.
//============================================================================
#ifndef vtk_m_cont_cuda_internal_DeviceAdapterAlgorithmThrust_h
#define vtk_m_cont_cuda_internal_DeviceAdapterAlgorithmThrust_h

#include <vtkm/cont/ArrayHandle.h>
#include <vtkm/cont/ArrayHandleCast.h>
#include <vtkm/cont/ErrorExecution.h>
#include <vtkm/Types.h>
#include <vtkm/TypeTraits.h>
#include <vtkm/UnaryPredicates.h>

#include <vtkm/cont/cuda/ErrorCuda.h>

#include <vtkm/cont/cuda/internal/DeviceAdapterTagCuda.h>
#include <vtkm/cont/cuda/internal/MakeThrustIterator.h>
#include <vtkm/cont/cuda/internal/ThrustExceptionHandler.h>

#include <vtkm/exec/cuda/internal/WrappedOperators.h>
#include <vtkm/exec/internal/ErrorMessageBuffer.h>
#include <vtkm/exec/internal/WorkletInvokeFunctor.h>



//#include <vtkm/cont/DeviceAdapterAlgorithm.h>
#include <vtkm/cont/internal/DeviceAdapterAlgorithmGeneral.h>

// Disable warnings we check vtkm for but Thrust does not.
VTKM_THIRDPARTY_PRE_INCLUDE
//our own custom thrust execution policy
#include <vtkm/exec/cuda/internal/ExecutionPolicy.h>
#include <thrust/advance.h>
#include <thrust/binary_search.h>
#include <thrust/copy.h>
#include <thrust/count.h>
#include <thrust/scan.h>
#include <thrust/sort.h>
#include <thrust/unique.h>
#include <thrust/system/cuda/vector.h>

#include <thrust/iterator/counting_iterator.h>
#include <thrust/system/cuda/execution_policy.h>
VTKM_THIRDPARTY_POST_INCLUDE

namespace vtkm {
namespace cont {

template<class DeviceAdapterTag>
class DeviceAdapterAlgorithm;


namespace cuda {
namespace internal {

static
__global__
void DetermineProperXGridSize(vtkm::UInt32 desired_size,
                              vtkm::UInt32* actual_size)
{
//used only to see if we can launch kernels with a x grid size that
//matches the max of the graphics card, or are we having to fall back
//to SM_2 grid sizes
 if(blockIdx.x != 0)
  {
  return;
  }
#if __CUDA_ARCH__ <= 200
  const vtkm::UInt32 maxXGridSizeForSM2 = 65535;
  *actual_size = maxXGridSizeForSM2;
#else
  *actual_size = desired_size;
#endif
}

template<class FunctorType>
__global__
void Schedule1DIndexKernel(FunctorType functor,
                           vtkm::Id numberOfKernelsInvoked,
                           vtkm::Id length)
{
  //Note a cuda launch can only handle at most 2B iterations of a kernel
  //because it holds all of the indexes inside UInt32, so for use to
  //handle datasets larger than 2B, we need to execute multiple kernels
  const vtkm::Id index = numberOfKernelsInvoked +
                         static_cast<vtkm::Id>(blockDim.x * blockIdx.x + threadIdx.x);
  if(index < length)
    {
    functor(index);
    }
}

template<class FunctorType>
__global__
void Schedule3DIndexKernel(FunctorType functor, dim3 size)
{
  const vtkm::Id3 index(
                        blockIdx.x*blockDim.x + threadIdx.x,
                        blockIdx.y*blockDim.y + threadIdx.y,
                        blockIdx.z*blockDim.z + threadIdx.z
                        );
  if (index[0] >= size.x || index[1] >= size.y || index[2] >= size.z)
    {
    return;
    }
  functor( index );
}

template<typename T, typename BinaryOperationType >
__global__
void SumExclusiveScan(T a, T b, T result,
                      BinaryOperationType binary_op)
{
  result = binary_op(a,b);
}

inline
void compute_block_size(dim3 rangeMax, dim3 blockSize3d, dim3& gridSize3d)
{
  gridSize3d.x = (rangeMax.x % blockSize3d.x != 0) ? (rangeMax.x / blockSize3d.x + 1) : (rangeMax.x / blockSize3d.x);
  gridSize3d.y = (rangeMax.y % blockSize3d.y != 0) ? (rangeMax.y / blockSize3d.y + 1) : (rangeMax.y / blockSize3d.y);
  gridSize3d.z = (rangeMax.z % blockSize3d.z != 0) ? (rangeMax.z / blockSize3d.z + 1) : (rangeMax.z / blockSize3d.z);
}

#ifdef ANALYZE_VTKM_SCHEDULER
class PerfRecord
{
public:

  PerfRecord(float elapsedT, dim3 block ):
    elapsedTime(elapsedT),
    blockSize(block)
    {

    }

  bool operator<(const PerfRecord& other) const
    { return elapsedTime < other.elapsedTime; }

  float elapsedTime;
  dim3 blockSize;
};

template<class Functor>
static void compare_3d_schedule_patterns(Functor functor, const vtkm::Id3& rangeMax)
{
  const dim3 ranges(static_cast<vtkm::UInt32>(rangeMax[0]),
                    static_cast<vtkm::UInt32>(rangeMax[1]),
                    static_cast<vtkm::UInt32>(rangeMax[2]) );
  std::vector< PerfRecord > results;
  vtkm::UInt32 indexTable[16] = {1, 2, 4, 8, 12, 16, 20, 24, 28, 30, 32, 64, 128, 256, 512, 1024};

  for(vtkm::UInt32 i=0; i < 16; i++)
    {
    for(vtkm::UInt32 j=0; j < 16; j++)
      {
      for(vtkm::UInt32 k=0; k < 16; k++)
        {
        cudaEvent_t start, stop;
        VTKM_CUDA_CALL(cudaEventCreate(&start));
        VTKM_CUDA_CALL(cudaEventCreate(&stop));

        dim3 blockSize3d(indexTable[i],indexTable[j],indexTable[k]);
        dim3 gridSize3d;

        if( (blockSize3d.x * blockSize3d.y * blockSize3d.z) >= 1024 ||
            (blockSize3d.x * blockSize3d.y * blockSize3d.z) <=  4 ||
            blockSize3d.z >= 64)
          {
          //cuda can't handle more than 1024 threads per block
          //so don't try if we compute higher than that

          //also don't try stupidly low numbers

          //cuda can't handle more than 64 threads in the z direction
          continue;
          }

        compute_block_size(ranges, blockSize3d, gridSize3d);
        VTKM_CUDA_CALL(cudaEventRecord(start, 0));
        Schedule3DIndexKernel<Functor> <<<gridSize3d, blockSize3d>>> (functor, ranges);
        VTKM_CUDA_CALL(cudaEventRecord(stop, 0));

        VTKM_CUDA_CALL(cudaEventSynchronize(stop));
        float elapsedTimeMilliseconds;
        VTKM_CUDA_CALL(cudaEventElapsedTime(&elapsedTimeMilliseconds, start, stop));

        VTKM_CUDA_CALL(cudaEventDestroy(start));
        VTKM_CUDA_CALL(cudaEventDestroy(stop));

        PerfRecord record(elapsedTimeMilliseconds, blockSize3d);
        results.push_back( record );
      }
    }
  }

  std::sort(results.begin(), results.end());
  const vtkm::Int64 size = static_cast<vtkm::Int64>(results.size());
  for(vtkm::Int64 i=1; i <= size; i++)
    {
    vtkm::UInt64 index = static_cast<vtkm::UInt64>(size-i);
    vtkm::UInt32 x = results[index].blockSize.x;
    vtkm::UInt32 y = results[index].blockSize.y;
    vtkm::UInt32 z = results[index].blockSize.z;
    float t = results[index].elapsedTime;

    std::cout << "BlockSize of: " << x << "," << y << "," << z << " required: " << t << std::endl;
    }

  std::cout << "flat array performance " << std::endl;
  {
  cudaEvent_t start, stop;
  VTKM_CUDA_CALL(cudaEventCreate(&start));
  VTKM_CUDA_CALL(cudaEventCreate(&stop));

  VTKM_CUDA_CALL(cudaEventRecord(start, 0));
  typedef
    vtkm::cont::cuda::internal::DeviceAdapterAlgorithmThrust<
          vtkm::cont::DeviceAdapterTagCuda > Algorithm;
  Algorithm::Schedule(functor, numInstances);
  VTKM_CUDA_CALL(cudaEventRecord(stop, 0));

  VTKM_CUDA_CALL(cudaEventSynchronize(stop));
  float elapsedTimeMilliseconds;
  VTKM_CUDA_CALL(cudaEventElapsedTime(&elapsedTimeMilliseconds, start, stop));

  VTKM_CUDA_CALL(cudaEventDestroy(start));
  VTKM_CUDA_CALL(cudaEventDestroy(stop));

  std::cout << "Flat index required: " << elapsedTimeMilliseconds << std::endl;
  }

  std::cout << "fixed 3d block size performance " << std::endl;
  {
  cudaEvent_t start, stop;
  VTKM_CUDA_CALL(cudaEventCreate(&start));
  VTKM_CUDA_CALL(cudaEventCreate(&stop));

  dim3 blockSize3d(64,2,1);
  dim3 gridSize3d;

  compute_block_size(ranges, blockSize3d, gridSize3d);
  VTKM_CUDA_CALL(cudaEventRecord(start, 0));
  Schedule3DIndexKernel<Functor> <<<gridSize3d, blockSize3d>>> (functor, ranges);
  VTKM_CUDA_CALL(cudaEventRecord(stop, 0));

  VTKM_CUDA_CALL(cudaEventSynchronize(stop));
  float elapsedTimeMilliseconds;
  VTKM_CUDA_CALL(cudaEventElapsedTime(&elapsedTimeMilliseconds, start, stop));

  VTKM_CUDA_CALL(cudaEventDestroy(start));
  VTKM_CUDA_CALL(cudaEventDestroy(stop));

  std::cout << "BlockSize of: " << blockSize3d.x << "," << blockSize3d.y << "," << blockSize3d.z << " required: " << elapsedTimeMilliseconds << std::endl;
  std::cout << "GridSize of: " << gridSize3d.x << "," << gridSize3d.y << "," << gridSize3d.z << " required: " << elapsedTimeMilliseconds << std::endl;
  }
}

#endif


/// This class can be subclassed to implement the DeviceAdapterAlgorithm for a
/// device that uses thrust as its implementation. The subclass should pass in
/// the correct device adapter tag as the template parameter.
///
template<class DeviceAdapterTag>
struct DeviceAdapterAlgorithmThrust : vtkm::cont::internal::DeviceAdapterAlgorithmGeneral<
               vtkm::cont::DeviceAdapterAlgorithm<vtkm::cont::DeviceAdapterTagCuda>,
               vtkm::cont::DeviceAdapterTagCuda>
{
  // Because of some funny code conversions in nvcc, kernels for devices have to
  // be public.
  #ifndef VTKM_CUDA
private:
  #endif
  template<class InputPortal, class OutputPortal>
  VTKM_CONT static void CopyPortal(const InputPortal &input,
                                          const OutputPortal &output)
  {
    try
    {
      ::thrust::copy(thrust::cuda::par,
                     IteratorBegin(input),
                     IteratorEnd(input),
                     IteratorBegin(output));
    }
    catch(...)
    {
      throwAsVTKmException();
    }
  }

  template<class ValueIterator,
  class StencilPortal,
  class OutputPortal,
  class UnaryPredicate>
  VTKM_CONT static
  vtkm::Id CopyIfPortal(ValueIterator valuesBegin,
                        ValueIterator valuesEnd,
                        StencilPortal stencil,
                        OutputPortal output,
                        UnaryPredicate unary_predicate)
  {
    typedef typename detail::IteratorTraits<OutputPortal>::IteratorType
    IteratorType;

    IteratorType outputBegin = IteratorBegin(output);

    typedef typename StencilPortal::ValueType ValueType;

    vtkm::exec::cuda::internal::WrappedUnaryPredicate<ValueType,
    UnaryPredicate> up(unary_predicate);

    try
    {
      IteratorType newLast = ::thrust::copy_if(thrust::cuda::par,
                                               valuesBegin,
                                               valuesEnd,
                                               IteratorBegin(stencil),
                                               outputBegin,
                                               up);
      return static_cast<vtkm::Id>( ::thrust::distance(outputBegin, newLast) );
    }
    catch(...)
    {
      throwAsVTKmException();
      return vtkm::Id(0);
    }


  }

  template<class ValuePortal,
  class StencilPortal,
  class OutputPortal,
  class UnaryPredicate>
  VTKM_CONT static
  vtkm::Id CopyIfPortal(ValuePortal values,
                        StencilPortal stencil,
                        OutputPortal output,
                        UnaryPredicate unary_predicate)
  {
    return CopyIfPortal(IteratorBegin(values),
                        IteratorEnd(values),
                        stencil,
                        output,
                        unary_predicate);
  }

  template<class InputPortal, class OutputPortal>
  VTKM_CONT static void CopySubRangePortal(const InputPortal &input,
                                                  vtkm::Id inputOffset,
                                                  vtkm::Id size,
                                                  const OutputPortal &output,
                                                  vtkm::Id outputOffset)
  {
    try
    {
      ::thrust::copy_n(thrust::cuda::par,
                       IteratorBegin(input)+inputOffset,
                       static_cast<std::size_t>(size),
                       IteratorBegin(output)+outputOffset);
    }
    catch(...)
    {
      throwAsVTKmException();
    }
  }

  template<class InputPortal, class ValuesPortal, class OutputPortal>
  VTKM_CONT static void LowerBoundsPortal(const InputPortal &input,
                                                 const ValuesPortal &values,
                                                 const OutputPortal &output)
  {
    typedef typename ValuesPortal::ValueType ValueType;
    LowerBoundsPortal(input, values, output, ::thrust::less<ValueType>() );
  }

  template<class InputPortal, class OutputPortal>
  VTKM_CONT static
  void LowerBoundsPortal(const InputPortal &input,
                         const OutputPortal &values_output)
  {
    typedef typename InputPortal::ValueType ValueType;
    LowerBoundsPortal(input, values_output, values_output,
                      ::thrust::less<ValueType>() );
  }

  template<class InputPortal, class ValuesPortal, class OutputPortal,
           class BinaryCompare>
  VTKM_CONT static void LowerBoundsPortal(const InputPortal &input,
                                                 const ValuesPortal &values,
                                                 const OutputPortal &output,
                                                 BinaryCompare binary_compare)
  {
    typedef typename InputPortal::ValueType ValueType;
    vtkm::exec::cuda::internal::WrappedBinaryPredicate<ValueType,
                                            BinaryCompare> bop(binary_compare);

    try
    {
      ::thrust::lower_bound(thrust::cuda::par,
                            IteratorBegin(input),
                            IteratorEnd(input),
                            IteratorBegin(values),
                            IteratorEnd(values),
                            IteratorBegin(output),
                            bop);
    }
    catch(...)
    {
      throwAsVTKmException();
    }
  }

  template<class InputPortal, typename T>
  VTKM_CONT static
  T ReducePortal(const InputPortal &input, T initialValue)
  {
    return ReducePortal(input,
                        initialValue,
                        ::thrust::plus<T>());
  }

  template<class InputPortal, typename T, class BinaryFunctor>
  VTKM_CONT static
  T ReducePortal(const InputPortal &input,
                 T initialValue,
                 BinaryFunctor binary_functor)
  {
    using fast_path = std::is_same< typename InputPortal::ValueType, T>;
    return ReducePortalImpl(input, initialValue, binary_functor, fast_path());
  }

  template<class InputPortal, typename T, class BinaryFunctor>
  VTKM_CONT static
  T ReducePortalImpl(const InputPortal &input, T initialValue,
                     BinaryFunctor binary_functor, std::true_type)
  {
    //The portal type and the initial value are the same so we can use
    //the thrust reduction algorithm
    vtkm::exec::cuda::internal::WrappedBinaryOperator<T,
                                                      BinaryFunctor> bop(binary_functor);

    try
    {
      return ::thrust::reduce(thrust::cuda::par,
                              IteratorBegin(input),
                              IteratorEnd(input),
                              initialValue,
                              bop);
    }
    catch(...)
    {
      throwAsVTKmException();
    }

    return initialValue;
  }

  template<class InputPortal, typename T, class BinaryFunctor>
  VTKM_CONT static
  T ReducePortalImpl(const InputPortal &input, T initialValue,
                     BinaryFunctor binary_functor,
                     std::false_type)
  {
    //The portal type and the initial value ARENT the same type so we have
    //to a slower approach, where we wrap the input portal inside a cast
    //portal
    using CastFunctor = vtkm::cont::internal::Cast<typename InputPortal::ValueType,T>;

    vtkm::exec::internal::ArrayPortalTransform< T, InputPortal, CastFunctor>
      castPortal(input);



    vtkm::exec::cuda::internal::WrappedBinaryOperator<T,
                                                      BinaryFunctor> bop(binary_functor);

    try
    {
      return ::thrust::reduce(thrust::cuda::par,
                              IteratorBegin(castPortal),
                              IteratorEnd(castPortal),
                              initialValue,
                              bop);
    }
    catch(...)
    {
      throwAsVTKmException();
    }

    return initialValue;
  }

  template<class KeysPortal, class ValuesPortal,
           class KeysOutputPortal, class ValueOutputPortal,
           class BinaryFunctor>
  VTKM_CONT static
  vtkm::Id ReduceByKeyPortal(const KeysPortal &keys,
                             const ValuesPortal& values,
                             const KeysOutputPortal &keys_output,
                             const ValueOutputPortal &values_output,
                             BinaryFunctor binary_functor)
  {
    typedef typename detail::IteratorTraits<KeysOutputPortal>::IteratorType
                                                             KeysIteratorType;
    typedef typename detail::IteratorTraits<ValueOutputPortal>::IteratorType
                                                             ValuesIteratorType;

    KeysIteratorType keys_out_begin = IteratorBegin(keys_output);
    ValuesIteratorType values_out_begin = IteratorBegin(values_output);

    ::thrust::pair< KeysIteratorType, ValuesIteratorType > result_iterators;

    ::thrust::equal_to<typename KeysPortal::ValueType> binaryPredicate;

    typedef typename ValuesPortal::ValueType ValueType;
    vtkm::exec::cuda::internal::WrappedBinaryOperator<ValueType,
                                                      BinaryFunctor> bop(binary_functor);


    try
    {
      result_iterators = ::thrust::reduce_by_key(vtkm_cuda_policy(),
                                                 IteratorBegin(keys),
                                                 IteratorEnd(keys),
                                                 IteratorBegin(values),
                                                 keys_out_begin,
                                                 values_out_begin,
                                                 binaryPredicate,
                                                 bop);
    }
    catch(...)
    {
      throwAsVTKmException();
    }

    return static_cast<vtkm::Id>( ::thrust::distance(keys_out_begin,
                                                     result_iterators.first) );
  }

  template<class InputPortal, class OutputPortal>
  VTKM_CONT static
  typename InputPortal::ValueType ScanExclusivePortal(const InputPortal &input,
                                                      const OutputPortal &output)
  {
    typedef typename OutputPortal::ValueType ValueType;

    return ScanExclusivePortal(input,
                               output,
                               (::thrust::plus<ValueType>()),
                               vtkm::TypeTraits<ValueType>::ZeroInitialization());

  }

    template<class InputPortal, class OutputPortal, class BinaryFunctor>
  VTKM_CONT static
  typename InputPortal::ValueType ScanExclusivePortal(const InputPortal &input,
                                                      const OutputPortal &output,
                                                      BinaryFunctor binaryOp,
                                        typename InputPortal::ValueType initialValue)
  {
    // Use iterator to get value so that thrust device_ptr has chance to handle
    // data on device.
    typedef typename OutputPortal::ValueType ValueType;

    //we have size three so that we can store the origin end value, the
    //new end value, and the sum of those two
    ::thrust::system::cuda::vector< ValueType > sum(3);
    try
    {

      //store the current value of the last position array in a separate cuda
      //memory location since the exclusive_scan will overwrite that value
      //once run
      ::thrust::copy_n(thrust::cuda::par,
                       IteratorEnd(input) - 1, 1, sum.begin());

      vtkm::exec::cuda::internal::WrappedBinaryOperator<ValueType,
                                                        BinaryFunctor> bop(binaryOp);

      typedef typename detail::IteratorTraits<OutputPortal>::IteratorType
                                                              IteratorType;
      IteratorType end = ::thrust::exclusive_scan(thrust::cuda::par,
                                                  IteratorBegin(input),
                                                  IteratorEnd(input),
                                                  IteratorBegin(output),
                                                  initialValue,
                                                  bop);

      //Store the new value for the end of the array. This is done because
      //with items such as the transpose array it is unsafe to pass the
      //portal to the SumExclusiveScan
      ::thrust::copy_n(thrust::cuda::par,
                       (end-1), 1, sum.begin()+1);


      //execute the binaryOp one last time on the device.
      SumExclusiveScan <<<1,1>>> (sum[0], sum[1], sum[2], bop);
    }
    catch(...)
    {
      throwAsVTKmException();
    }
    return sum[2];
  }

  template<class InputPortal, class OutputPortal>
  VTKM_CONT static
  typename InputPortal::ValueType ScanInclusivePortal(const InputPortal &input,
                                                      const OutputPortal &output)
  {
    typedef typename OutputPortal::ValueType ValueType;
    return ScanInclusivePortal(input, output, ::thrust::plus<ValueType>() );
  }

  template<class InputPortal, class OutputPortal, class BinaryFunctor>
  VTKM_CONT static
  typename InputPortal::ValueType ScanInclusivePortal(const InputPortal &input,
                                                      const OutputPortal &output,
                                                      BinaryFunctor binary_functor)
  {
    typedef typename OutputPortal::ValueType ValueType;
    vtkm::exec::cuda::internal::WrappedBinaryOperator<ValueType,
                                                      BinaryFunctor> bop(binary_functor);

    typedef typename detail::IteratorTraits<OutputPortal>::IteratorType
                                                            IteratorType;

    try
    {
      IteratorType end = ::thrust::inclusive_scan(thrust::cuda::par,
                                                  IteratorBegin(input),
                                                  IteratorEnd(input),
                                                  IteratorBegin(output),
                                                  bop);
      return *(end-1);
    }
    catch(...)
    {
      throwAsVTKmException();
      return typename InputPortal::ValueType();
    }

    //return the value at the last index in the array, as that is the sum

  }

  template<typename KeysPortal, typename ValuesPortal, typename OutputPortal>
  VTKM_CONT static
  typename ValuesPortal::ValueType ScanInclusiveByKeyPortal(const KeysPortal &keys,
                                                            const ValuesPortal &values,
                                                            const OutputPortal &output)
  {
    using KeyType = typename KeysPortal::ValueType;
    typedef typename OutputPortal::ValueType ValueType;
    return ScanInclusiveByKeyPortal(keys, values, output,
                                    ::thrust::equal_to<KeyType>(),
                                    ::thrust::plus<ValueType>());
  }

  template<typename KeysPortal, typename ValuesPortal, typename OutputPortal,
    typename BinaryPredicate, typename AssociativeOperator>
  VTKM_CONT static
  typename ValuesPortal::ValueType ScanInclusiveByKeyPortal(const KeysPortal &keys,
                                                            const ValuesPortal &values,
                                                            const OutputPortal &output,
                                                            BinaryPredicate binary_predicate,
                                                            AssociativeOperator binary_operator)
  {
    typedef typename KeysPortal::ValueType KeyType;
    vtkm::exec::cuda::internal::WrappedBinaryOperator<KeyType,
      BinaryPredicate> bpred(binary_predicate);
    typedef typename OutputPortal::ValueType ValueType;
    vtkm::exec::cuda::internal::WrappedBinaryOperator<ValueType,
      AssociativeOperator> bop(binary_operator);


    typedef typename detail::IteratorTraits<OutputPortal>::IteratorType
      IteratorType;
    try
    {
      IteratorType end = ::thrust::inclusive_scan_by_key(thrust::cuda::par,
                                                         IteratorBegin(keys),
                                                         IteratorEnd(keys),
                                                         IteratorBegin(values),
                                                         IteratorBegin(output),
                                                         bpred,
                                                         bop);
      return *(end-1);
    }
    catch(...)
    {
      throwAsVTKmException();
      return typename ValuesPortal::ValueType();
    }

    //return the value at the last index in the array, as that is the sum

  }

  template<typename KeysPortal, typename ValuesPortal, typename OutputPortal>
  VTKM_CONT static
  void ScanExclusiveByKeyPortal(const KeysPortal &keys,
                                const ValuesPortal &values,
                                const OutputPortal &output)
  {
    using KeyType = typename KeysPortal::ValueType;
    typedef typename OutputPortal::ValueType ValueType;
    ScanExclusiveByKeyPortal(keys, values, output,
                             vtkm::TypeTraits<ValueType>::ZeroInitialization(),
                             ::thrust::equal_to<KeyType>(),
                             ::thrust::plus<ValueType>());
  }

  template<typename KeysPortal, typename ValuesPortal, typename OutputPortal, typename T,
    typename BinaryPredicate, typename AssociativeOperator>
  VTKM_CONT static
  void ScanExclusiveByKeyPortal(const KeysPortal &keys,
                                const ValuesPortal &values,
                                const OutputPortal &output,
                                T initValue,
                                BinaryPredicate binary_predicate,
                                AssociativeOperator binary_operator)
  {
    typedef typename KeysPortal::ValueType KeyType;
    vtkm::exec::cuda::internal::WrappedBinaryOperator<KeyType,
      BinaryPredicate> bpred(binary_predicate);
    typedef typename OutputPortal::ValueType ValueType;
    vtkm::exec::cuda::internal::WrappedBinaryOperator<ValueType,
      AssociativeOperator> bop(binary_operator);


    typedef typename detail::IteratorTraits<OutputPortal>::IteratorType
      IteratorType;
    try
    {
      IteratorType end = ::thrust::exclusive_scan_by_key(thrust::cuda::par,
                                                         IteratorBegin(keys),
                                                         IteratorEnd(keys),
                                                         IteratorBegin(values),
                                                         IteratorBegin(output),
                                                         initValue,
                                                         bpred,
                                                         bop);
      return;
    }
    catch(...)
    {
      throwAsVTKmException();
      return;
    }

    //return the value at the last index in the array, as that is the sum

  }

  template<class ValuesPortal>
  VTKM_CONT static void SortPortal(const ValuesPortal &values)
  {
    typedef typename ValuesPortal::ValueType ValueType;
    SortPortal(values, ::thrust::less<ValueType>());
  }

  template<class ValuesPortal, class BinaryCompare>
  VTKM_CONT static void SortPortal(const ValuesPortal &values,
                                         BinaryCompare binary_compare)
  {
    typedef typename ValuesPortal::ValueType ValueType;
    vtkm::exec::cuda::internal::WrappedBinaryPredicate<ValueType,
                                                       BinaryCompare> bop(binary_compare);
    try
    {
      ::thrust::sort(vtkm_cuda_policy(),
                     IteratorBegin(values),
                     IteratorEnd(values),
                     bop);
    }
    catch(...)
    {
      throwAsVTKmException();
    }
  }


  template<class KeysPortal, class ValuesPortal>
  VTKM_CONT static void SortByKeyPortal(const KeysPortal &keys,
                                               const ValuesPortal &values)
  {
    typedef typename KeysPortal::ValueType ValueType;
    SortByKeyPortal(keys,values,::thrust::less<ValueType>());
  }

  template<class KeysPortal, class ValuesPortal, class BinaryCompare>
  VTKM_CONT static void SortByKeyPortal(const KeysPortal &keys,
                                               const ValuesPortal &values,
                                               BinaryCompare binary_compare)
  {
    typedef typename KeysPortal::ValueType ValueType;
    vtkm::exec::cuda::internal::WrappedBinaryPredicate<ValueType,
                                                       BinaryCompare> bop(binary_compare);
    try
    {
      ::thrust::sort_by_key(vtkm_cuda_policy(),
                            IteratorBegin(keys),
                            IteratorEnd(keys),
                            IteratorBegin(values),
                            bop);
    }
    catch(...)
    {
      throwAsVTKmException();
    }
  }

  template<class ValuesPortal>
  VTKM_CONT static
  vtkm::Id UniquePortal(const ValuesPortal values)
  {
    typedef typename detail::IteratorTraits<ValuesPortal>::IteratorType
                                                            IteratorType;
    try
    {
      IteratorType begin = IteratorBegin(values);
      IteratorType newLast = ::thrust::unique(thrust::cuda::par,
                                              begin,
                                              IteratorEnd(values));
      return static_cast<vtkm::Id>( ::thrust::distance(begin, newLast) );
    }
    catch(...)
    {
      throwAsVTKmException();
      return vtkm::Id(0);
    }
  }

  template<class ValuesPortal, class BinaryCompare>
  VTKM_CONT static
  vtkm::Id UniquePortal(const ValuesPortal values, BinaryCompare binary_compare)
  {
    typedef typename detail::IteratorTraits<ValuesPortal>::IteratorType
                                                            IteratorType;
    typedef typename ValuesPortal::ValueType ValueType;

    vtkm::exec::cuda::internal::WrappedBinaryPredicate<ValueType,
                                                       BinaryCompare> bop(binary_compare);
    try
    {
      IteratorType begin = IteratorBegin(values);
      IteratorType newLast = ::thrust::unique(thrust::cuda::par,
                                              begin,
                                              IteratorEnd(values),
                                              bop);
      return static_cast<vtkm::Id>( ::thrust::distance(begin, newLast) );
    }
    catch(...)
    {
      throwAsVTKmException();
      return vtkm::Id(0);
    }
  }

  template<class InputPortal, class ValuesPortal, class OutputPortal>
  VTKM_CONT static
  void UpperBoundsPortal(const InputPortal &input,
                         const ValuesPortal &values,
                         const OutputPortal &output)
  {
    try
    {
      ::thrust::upper_bound(thrust::cuda::par,
                            IteratorBegin(input),
                            IteratorEnd(input),
                            IteratorBegin(values),
                            IteratorEnd(values),
                            IteratorBegin(output));
    }
    catch(...)
    {
      throwAsVTKmException();
    }
  }

  template<class InputPortal, class ValuesPortal, class OutputPortal,
           class BinaryCompare>
  VTKM_CONT static void UpperBoundsPortal(const InputPortal &input,
                                                const ValuesPortal &values,
                                                const OutputPortal &output,
                                                BinaryCompare binary_compare)
  {
    typedef typename OutputPortal::ValueType ValueType;

    vtkm::exec::cuda::internal::WrappedBinaryPredicate<ValueType,
                                                       BinaryCompare> bop(binary_compare);
    try
    {
      ::thrust::upper_bound(thrust::cuda::par,
                            IteratorBegin(input),
                            IteratorEnd(input),
                            IteratorBegin(values),
                            IteratorEnd(values),
                            IteratorBegin(output),
                            bop);
    }
    catch(...)
    {
      throwAsVTKmException();
    }
  }

  template<class InputPortal, class OutputPortal>
  VTKM_CONT static
  void UpperBoundsPortal(const InputPortal &input,
                         const OutputPortal &values_output)
  {
    try
    {
      ::thrust::upper_bound(thrust::cuda::par,
                            IteratorBegin(input),
                            IteratorEnd(input),
                            IteratorBegin(values_output),
                            IteratorEnd(values_output),
                            IteratorBegin(values_output));
    }
    catch(...)
    {
      throwAsVTKmException();
    }
  }

//-----------------------------------------------------------------------------

public:
  template<typename T, typename U, class SIn, class SOut>
  VTKM_CONT static void Copy(
      const vtkm::cont::ArrayHandle<T,SIn> &input,
      vtkm::cont::ArrayHandle<U,SOut> &output)
  {
    const vtkm::Id inSize = input.GetNumberOfValues();
    CopyPortal(input.PrepareForInput(DeviceAdapterTag()),
               output.PrepareForOutput(inSize, DeviceAdapterTag()));
  }

  template<typename T,
  typename U,
  class SIn,
  class SStencil,
  class SOut>
  VTKM_CONT static void CopyIf(
    const vtkm::cont::ArrayHandle<U,SIn>& input,
    const vtkm::cont::ArrayHandle<T,SStencil>& stencil,
    vtkm::cont::ArrayHandle<U,SOut>& output)
  {
    vtkm::Id size = stencil.GetNumberOfValues();
    vtkm::Id newSize = CopyIfPortal(input.PrepareForInput(DeviceAdapterTag()),
                                    stencil.PrepareForInput(DeviceAdapterTag()),
                                    output.PrepareForOutput(size, DeviceAdapterTag()),
                                    ::vtkm::NotZeroInitialized()); //yes on the stencil
    output.Shrink(newSize);
  }

  template<typename T,
  typename U,
  class SIn,
  class SStencil,
  class SOut,
  class UnaryPredicate>
  VTKM_CONT static void CopyIf(
    const vtkm::cont::ArrayHandle<U,SIn>& input,
    const vtkm::cont::ArrayHandle<T,SStencil>& stencil,
    vtkm::cont::ArrayHandle<U,SOut>& output,
    UnaryPredicate unary_predicate)
  {
    vtkm::Id size = stencil.GetNumberOfValues();
    vtkm::Id newSize = CopyIfPortal(input.PrepareForInput(DeviceAdapterTag()),
                                    stencil.PrepareForInput(DeviceAdapterTag()),
                                    output.PrepareForOutput(size, DeviceAdapterTag()),
                                    unary_predicate);
    output.Shrink(newSize);
  }

  template<typename T, typename U, class SIn, class SOut>
  VTKM_CONT static bool CopySubRange(
      const vtkm::cont::ArrayHandle<T,SIn> &input,
      vtkm::Id inputStartIndex,
      vtkm::Id numberOfElementsToCopy,
      vtkm::cont::ArrayHandle<U,SOut> &output,
      vtkm::Id outputIndex = 0)
  {
    const vtkm::Id inSize = input.GetNumberOfValues();
    if(inputStartIndex < 0 ||
       numberOfElementsToCopy < 0 ||
       outputIndex < 0 ||
       inputStartIndex >= inSize)
    {  //invalid parameters
      return false;
    }

    //determine if the numberOfElementsToCopy needs to be reduced
    if(inSize < (inputStartIndex + numberOfElementsToCopy))
      { //adjust the size
      numberOfElementsToCopy = (inSize - inputStartIndex);
      }

    const vtkm::Id outSize = output.GetNumberOfValues();
    const vtkm::Id copyOutEnd = outputIndex + numberOfElementsToCopy;
    if(outSize < copyOutEnd)
    { //output is not large enough
      if(outSize == 0)
      { //since output has nothing, just need to allocate to correct length
        output.Allocate(copyOutEnd);
      }
      else
      { //we currently have data in this array, so preserve it in the new
        //resized array
        vtkm::cont::ArrayHandle<U, SOut> temp;
        temp.Allocate(copyOutEnd);
        CopySubRange(output, 0, outSize, temp);
        output = temp;
      }
    }
    CopySubRangePortal(input.PrepareForInput(DeviceAdapterTag()),
                       inputStartIndex,
                       numberOfElementsToCopy,
                       output.PrepareForInPlace(DeviceAdapterTag()),
                       outputIndex);
    return true;
  }

  template<typename T, class SIn, class SVal, class SOut>
  VTKM_CONT static void LowerBounds(
      const vtkm::cont::ArrayHandle<T,SIn>& input,
      const vtkm::cont::ArrayHandle<T,SVal>& values,
      vtkm::cont::ArrayHandle<vtkm::Id,SOut>& output)
  {
    vtkm::Id numberOfValues = values.GetNumberOfValues();
    LowerBoundsPortal(input.PrepareForInput(DeviceAdapterTag()),
                      values.PrepareForInput(DeviceAdapterTag()),
                      output.PrepareForOutput(numberOfValues, DeviceAdapterTag()));
  }

  template<typename T, class SIn, class SVal, class SOut, class BinaryCompare>
  VTKM_CONT static void LowerBounds(
      const vtkm::cont::ArrayHandle<T,SIn>& input,
      const vtkm::cont::ArrayHandle<T,SVal>& values,
      vtkm::cont::ArrayHandle<vtkm::Id,SOut>& output,
      BinaryCompare binary_compare)
  {
    vtkm::Id numberOfValues = values.GetNumberOfValues();
    LowerBoundsPortal(input.PrepareForInput(DeviceAdapterTag()),
                      values.PrepareForInput(DeviceAdapterTag()),
                      output.PrepareForOutput(numberOfValues, DeviceAdapterTag()),
                      binary_compare);
  }

  template<class SIn, class SOut>
  VTKM_CONT static void LowerBounds(
      const vtkm::cont::ArrayHandle<vtkm::Id,SIn> &input,
      vtkm::cont::ArrayHandle<vtkm::Id,SOut> &values_output)
  {
    LowerBoundsPortal(input.PrepareForInput(DeviceAdapterTag()),
                      values_output.PrepareForInPlace(DeviceAdapterTag()));
  }

 template<typename T, typename U, class SIn>
  VTKM_CONT static U Reduce(
      const vtkm::cont::ArrayHandle<T,SIn> &input,
      U initialValue)
  {
    const vtkm::Id numberOfValues = input.GetNumberOfValues();
    if (numberOfValues <= 0)
      {
      return initialValue;
      }
    return ReducePortal(input.PrepareForInput( DeviceAdapterTag() ),
                        initialValue);
  }

 template<typename T, typename U, class SIn, class BinaryFunctor>
  VTKM_CONT static U Reduce(
      const vtkm::cont::ArrayHandle<T,SIn> &input,
      U initialValue,
      BinaryFunctor binary_functor)
  {
    const vtkm::Id numberOfValues = input.GetNumberOfValues();
    if (numberOfValues <= 0)
      {
      return initialValue;
      }
    return ReducePortal(input.PrepareForInput( DeviceAdapterTag() ),
                        initialValue,
                        binary_functor);
  }

 template<typename T, typename U, class KIn, class VIn, class KOut, class VOut,
          class BinaryFunctor>
  VTKM_CONT static void ReduceByKey(
      const vtkm::cont::ArrayHandle<T,KIn> &keys,
      const vtkm::cont::ArrayHandle<U,VIn> &values,
      vtkm::cont::ArrayHandle<T,KOut> &keys_output,
      vtkm::cont::ArrayHandle<U,VOut> &values_output,
      BinaryFunctor binary_functor)
  {
    //there is a concern that by default we will allocate too much
    //space for the keys/values output. 1 option is to
    const vtkm::Id numberOfValues = keys.GetNumberOfValues();
    if (numberOfValues <= 0)
      {
      return;
      }
    vtkm::Id reduced_size =
            ReduceByKeyPortal(keys.PrepareForInput( DeviceAdapterTag() ),
                              values.PrepareForInput( DeviceAdapterTag() ),
                              keys_output.PrepareForOutput( numberOfValues, DeviceAdapterTag() ),
                              values_output.PrepareForOutput( numberOfValues, DeviceAdapterTag() ),
                              binary_functor);

    keys_output.Shrink( reduced_size );
    values_output.Shrink( reduced_size );
  }

  template<typename T, class SIn, class SOut>
  VTKM_CONT static T ScanExclusive(
      const vtkm::cont::ArrayHandle<T,SIn> &input,
      vtkm::cont::ArrayHandle<T,SOut>& output)
  {
    const vtkm::Id numberOfValues = input.GetNumberOfValues();
    if (numberOfValues <= 0)
      {
      output.PrepareForOutput(0, DeviceAdapterTag());
      return vtkm::TypeTraits<T>::ZeroInitialization();
      }

    //We need call PrepareForInput on the input argument before invoking a
    //function. The order of execution of parameters of a function is undefined
    //so we need to make sure input is called before output, or else in-place
    //use case breaks.
    input.PrepareForInput(DeviceAdapterTag());
    return ScanExclusivePortal(input.PrepareForInput(DeviceAdapterTag()),
                               output.PrepareForOutput(numberOfValues, DeviceAdapterTag()));
  }

  template<typename T, class SIn, class SOut, class BinaryFunctor>
  VTKM_CONT static T ScanExclusive(
      const vtkm::cont::ArrayHandle<T,SIn> &input,
      vtkm::cont::ArrayHandle<T,SOut>& output,
      BinaryFunctor binary_functor,
      const T& initialValue)
  {
    const vtkm::Id numberOfValues = input.GetNumberOfValues();
    if (numberOfValues <= 0)
      {
      output.PrepareForOutput(0, DeviceAdapterTag());
      return vtkm::TypeTraits<T>::ZeroInitialization();
      }

    //We need call PrepareForInput on the input argument before invoking a
    //function. The order of execution of parameters of a function is undefined
    //so we need to make sure input is called before output, or else in-place
    //use case breaks.
    input.PrepareForInput(DeviceAdapterTag());
    return ScanExclusivePortal(
        input.PrepareForInput(DeviceAdapterTag()),
        output.PrepareForOutput(numberOfValues, DeviceAdapterTag()),
        binary_functor,
        initialValue);
  }

  template<typename T, class SIn, class SOut>
  VTKM_CONT static T ScanInclusive(
      const vtkm::cont::ArrayHandle<T,SIn> &input,
      vtkm::cont::ArrayHandle<T,SOut>& output)
  {
    const vtkm::Id numberOfValues = input.GetNumberOfValues();
    if (numberOfValues <= 0)
      {
      output.PrepareForOutput(0, DeviceAdapterTag());
      return vtkm::TypeTraits<T>::ZeroInitialization();
      }

    //We need call PrepareForInput on the input argument before invoking a
    //function. The order of execution of parameters of a function is undefined
    //so we need to make sure input is called before output, or else in-place
    //use case breaks.
    input.PrepareForInput(DeviceAdapterTag());
    return ScanInclusivePortal(input.PrepareForInput(DeviceAdapterTag()),
                               output.PrepareForOutput(numberOfValues, DeviceAdapterTag()));
  }

  template<typename T, class SIn, class SOut, class BinaryFunctor>
  VTKM_CONT static T ScanInclusive(
      const vtkm::cont::ArrayHandle<T,SIn> &input,
      vtkm::cont::ArrayHandle<T,SOut>& output,
      BinaryFunctor binary_functor)
  {
    const vtkm::Id numberOfValues = input.GetNumberOfValues();
    if (numberOfValues <= 0)
      {
      output.PrepareForOutput(0, DeviceAdapterTag());
      return vtkm::TypeTraits<T>::ZeroInitialization();
      }

    //We need call PrepareForInput on the input argument before invoking a
    //function. The order of execution of parameters of a function is undefined
    //so we need to make sure input is called before output, or else in-place
    //use case breaks.
    input.PrepareForInput(DeviceAdapterTag());
    return ScanInclusivePortal(input.PrepareForInput(DeviceAdapterTag()),
                               output.PrepareForOutput(numberOfValues, DeviceAdapterTag()),
                               binary_functor);
  }

  template<typename T, typename U, typename KIn, typename VIn, typename VOut>
  VTKM_CONT static T ScanInclusiveByKey(
    const vtkm::cont::ArrayHandle<T, KIn>& keys,
    const vtkm::cont::ArrayHandle<U, VIn>& values,
    vtkm::cont::ArrayHandle<U, VOut>& output)
  {
    const vtkm::Id numberOfValues = keys.GetNumberOfValues();
    if (numberOfValues <= 0)
    {
      output.PrepareForOutput(0, DeviceAdapterTag());
      return vtkm::TypeTraits<T>::ZeroInitialization();
    }

    //We need call PrepareForInput on the input argument before invoking a
    //function. The order of execution of parameters of a function is undefined
    //so we need to make sure input is called before output, or else in-place
    //use case breaks.
    keys.PrepareForInput(DeviceAdapterTag());
    values.PrepareForInput(DeviceAdapterTag());
    return ScanInclusiveByKeyPortal(keys.PrepareForInput(DeviceAdapterTag()),
                                    values.PrepareForInput(DeviceAdapterTag()),
                                    output.PrepareForOutput(numberOfValues, DeviceAdapterTag()));
  }

  template<typename T, typename U, typename KIn, typename VIn, typename VOut,
    typename BinaryFunctor>
  VTKM_CONT static T ScanInclusiveByKey(
    const vtkm::cont::ArrayHandle<T, KIn>& keys,
    const vtkm::cont::ArrayHandle<U, VIn>& values,
    vtkm::cont::ArrayHandle<U, VOut>& output,
    BinaryFunctor binary_functor)
  {
    const vtkm::Id numberOfValues = keys.GetNumberOfValues();
    if (numberOfValues <= 0)
    {
      output.PrepareForOutput(0, DeviceAdapterTag());
      return vtkm::TypeTraits<T>::ZeroInitialization();
    }

    //We need call PrepareForInput on the input argument before invoking a
    //function. The order of execution of parameters of a function is undefined
    //so we need to make sure input is called before output, or else in-place
    //use case breaks.
    keys.PrepareForInput(DeviceAdapterTag());
    values.PrepareForInput(DeviceAdapterTag());
    return ScanInclusiveByKeyPortal(keys.PrepareForInput(DeviceAdapterTag()),
                                    values.PrepareForInput(DeviceAdapterTag()),
                                    output.PrepareForOutput(numberOfValues, DeviceAdapterTag()),
                                    ::thrust::equal_to<T>(),
                                    binary_functor);
  }

  template<typename T, typename U, typename KIn, typename VIn, typename VOut>
  VTKM_CONT static void ScanExclusiveByKey(
    const vtkm::cont::ArrayHandle<T, KIn>& keys,
    const vtkm::cont::ArrayHandle<U, VIn>& values,
    vtkm::cont::ArrayHandle<U, VOut>& output)
  {
    const vtkm::Id numberOfValues = keys.GetNumberOfValues();
    if (numberOfValues <= 0)
    {
      output.PrepareForOutput(0, DeviceAdapterTag());
      return vtkm::TypeTraits<T>::ZeroInitialization();
    }

    //We need call PrepareForInput on the input argument before invoking a
    //function. The order of execution of parameters of a function is undefined
    //so we need to make sure input is called before output, or else in-place
    //use case breaks.
    keys.PrepareForInput(DeviceAdapterTag());
    values.PrepareForInput(DeviceAdapterTag());
    ScanExnclusiveByKeyPortal(keys.PrepareForInput(DeviceAdapterTag()),
                              values.PrepareForInput(DeviceAdapterTag()),
                              output.PrepareForOutput(numberOfValues, DeviceAdapterTag()),
                              vtkm::TypeTraits<T>::ZeroInitialization(),
                              vtkm::Add());
  }

  template<typename T, typename U, typename KIn, typename VIn, typename VOut,
    typename BinaryFunctor>
  VTKM_CONT static void ScanExclusiveByKey(
    const vtkm::cont::ArrayHandle<T, KIn>& keys,
    const vtkm::cont::ArrayHandle<U, VIn>& values,
    vtkm::cont::ArrayHandle<U, VOut>& output,
    const U& initialValue,
    BinaryFunctor binary_functor)
  {
    const vtkm::Id numberOfValues = keys.GetNumberOfValues();
    if (numberOfValues <= 0)
    {
      output.PrepareForOutput(0, DeviceAdapterTag());
      return;
    }

    //We need call PrepareForInput on the input argument before invoking a
    //function. The order of execution of parameters of a function is undefined
    //so we need to make sure input is called before output, or else in-place
    //use case breaks.
    keys.PrepareForInput(DeviceAdapterTag());
    values.PrepareForInput(DeviceAdapterTag());
    ScanExclusiveByKeyPortal(keys.PrepareForInput(DeviceAdapterTag()),
                             values.PrepareForInput(DeviceAdapterTag()),
                             output.PrepareForOutput(numberOfValues, DeviceAdapterTag()),
                             initialValue,
                             ::thrust::equal_to<T>(),
                             binary_functor);
  }
// Because of some funny code conversions in nvcc, kernels for devices have to
// be public.
#ifndef VTKM_CUDA
private:
#endif
  // we use cuda pinned memory to reduce the amount of synchronization
  // and mem copies between the host and device.
  VTKM_CONT
  static char* GetPinnedErrorArray(vtkm::Id &arraySize, char** hostPointer)
    {
    const vtkm::Id ERROR_ARRAY_SIZE = 1024;
    static bool errorArrayInit = false;
    static char* hostPtr = nullptr;
    static char* devicePtr = nullptr;
    if( !errorArrayInit )
      {
      VTKM_CUDA_CALL(cudaMallocHost( (void**)&hostPtr,
                                     ERROR_ARRAY_SIZE,
                                     cudaHostAllocMapped ));
      VTKM_CUDA_CALL(cudaHostGetDevicePointer(&devicePtr, hostPtr, 0));
      errorArrayInit = true;
      }
    //set the size of the array
    arraySize = ERROR_ARRAY_SIZE;

    //specify the host pointer to the memory
    *hostPointer = hostPtr;
    (void) hostPointer;
    return devicePtr;
    }

  // we query cuda for the max blocks per grid for 1D scheduling
  // and cache the values in static variables
  VTKM_CONT
  static vtkm::Vec<vtkm::UInt32,3> GetMaxGridOfThreadBlocks()
    {
    static bool gridQueryInit = false;
    static vtkm::Vec< vtkm::UInt32, 3> maxGridSize;
    if( !gridQueryInit )
      {
      gridQueryInit = true;
      int currDevice;
      VTKM_CUDA_CALL(cudaGetDevice(&currDevice)); //get deviceid from cuda

      cudaDeviceProp properties;
      VTKM_CUDA_CALL(cudaGetDeviceProperties(&properties, currDevice));
      maxGridSize[0] = static_cast<vtkm::UInt32>(properties.maxGridSize[0]);
      maxGridSize[1] = static_cast<vtkm::UInt32>(properties.maxGridSize[1]);
      maxGridSize[2] = static_cast<vtkm::UInt32>(properties.maxGridSize[2]);

      //Note: While in practice SM_3+ devices can schedule up to (2^31-1) grids
      //in the X direction, it is dependent on the code being compiled for SM3+.
      //If not, it falls back to SM_2 limitation of 65535 being the largest grid
      //size.
      //Now since SM architecture is only available inside kernels we have to
      //invoke one to see what the actual limit is for our device.  So that is
      //what we are going to do next, and than we will store that result

      vtkm::UInt32 *dev_actual_size;
      VTKM_CUDA_CALL(
            cudaMalloc( (void**)&dev_actual_size, sizeof(vtkm::UInt32) )
            );
      DetermineProperXGridSize <<<1,1>>> (maxGridSize[0], dev_actual_size);
      VTKM_CUDA_CALL(cudaDeviceSynchronize());
      VTKM_CUDA_CALL(cudaMemcpy( &maxGridSize[0],
                                 dev_actual_size,
                                 sizeof(vtkm::UInt32),
                                 cudaMemcpyDeviceToHost ));
      VTKM_CUDA_CALL(cudaFree(dev_actual_size));
      }
    return maxGridSize;
    }

public:
  template<class Functor>
  VTKM_CONT static void Schedule(Functor functor, vtkm::Id numInstances)
  {
    VTKM_ASSERT(numInstances >= 0);
    if (numInstances < 1)
    {
      // No instances means nothing to run. Just return.
      return;
    }

    //since the memory is pinned we can access it safely on the host
    //without a memcpy
    vtkm::Id errorArraySize = 0;
    char* hostErrorPtr = nullptr;
    char* deviceErrorPtr = GetPinnedErrorArray(errorArraySize, &hostErrorPtr);

    //clear the first character which means that we don't contain an error
    hostErrorPtr[0] = '\0';

    vtkm::exec::internal::ErrorMessageBuffer errorMessage( deviceErrorPtr,
                                                           errorArraySize);

    functor.SetErrorMessageBuffer(errorMessage);

    const vtkm::Id blockSizeAsId = 128;
    const vtkm::UInt32 blockSize = 128;
    const vtkm::UInt32 maxblocksPerLaunch = GetMaxGridOfThreadBlocks()[0];
    const vtkm::UInt32 totalBlocks = static_cast<vtkm::UInt32>(
                          (numInstances + blockSizeAsId - 1) / blockSizeAsId);

    //Note a cuda launch can only handle at most 2B iterations of a kernel
    //because it holds all of the indexes inside UInt32, so for use to
    //handle datasets larger than 2B, we need to execute multiple kernels
    if(totalBlocks < maxblocksPerLaunch)
      {
      Schedule1DIndexKernel<Functor> <<<totalBlocks, blockSize>>> (functor,
                                                                   vtkm::Id(0),
                                                                   numInstances);
      }
    else
      {
      const vtkm::Id numberOfKernelsToRun = blockSizeAsId * static_cast<vtkm::Id>(maxblocksPerLaunch);
      for(vtkm::Id numberOfKernelsInvoked = 0;
          numberOfKernelsInvoked < numInstances;
          numberOfKernelsInvoked += numberOfKernelsToRun)
        {
        Schedule1DIndexKernel<Functor> <<<maxblocksPerLaunch, blockSize>>> (functor,
                                                                            numberOfKernelsInvoked,
                                                                            numInstances);
        }
      }

    //sync so that we can check the results of the call.
    //In the future I want move this before the schedule call, and throwing
    //an exception if the previous schedule wrote an error. This would help
    //cuda to run longer before we hard sync.
    VTKM_CUDA_CALL(cudaDeviceSynchronize());

    //check what the value is
    if (hostErrorPtr[0] != '\0')
      {
      throw vtkm::cont::ErrorExecution(hostErrorPtr);
      }
  }

  template<class Functor>
  VTKM_CONT
  static void Schedule(Functor functor, const vtkm::Id3& rangeMax)
  {
    VTKM_ASSERT((rangeMax[0]>=0) && (rangeMax[1]>=0) && (rangeMax[2]>=0));
    if ((rangeMax[0]<1) || (rangeMax[1]<1) || (rangeMax[2]<1))
    {
      // No instances means nothing to run. Just return.
      return;
    }

    //since the memory is pinned we can access it safely on the host
    //without a memcpy
    vtkm::Id errorArraySize = 0;
    char* hostErrorPtr = nullptr;
    char* deviceErrorPtr = GetPinnedErrorArray(errorArraySize, &hostErrorPtr);

    //clear the first character which means that we don't contain an error
    hostErrorPtr[0] = '\0';

    vtkm::exec::internal::ErrorMessageBuffer errorMessage( deviceErrorPtr,
                                                           errorArraySize);

    functor.SetErrorMessageBuffer(errorMessage);

#ifdef ANALYZE_VTKM_SCHEDULER
    //requires the errormessage buffer be set
    compare_3d_schedule_patterns(functor,rangeMax);
#endif
    const dim3 ranges(static_cast<vtkm::UInt32>(rangeMax[0]),
                      static_cast<vtkm::UInt32>(rangeMax[1]),
                      static_cast<vtkm::UInt32>(rangeMax[2]) );


    //currently we presume that 3d scheduling access patterns prefer accessing
    //memory in the X direction. Also should be good for thin in the Z axis
    //algorithms.
    dim3 blockSize3d(64,2,1);

    //handle the simple use case of 'bad' datasets which are thin in X
    //but larger in the other directions, allowing us decent performance with
    //that use case.
    if(rangeMax[0] <= 128 &&
       (rangeMax[0] < rangeMax[1] || rangeMax[0] < rangeMax[2]) )
      {
      blockSize3d = dim3(16,4,4);
      }

    dim3 gridSize3d;
    compute_block_size(ranges, blockSize3d, gridSize3d);

    Schedule3DIndexKernel<Functor> <<<gridSize3d, blockSize3d>>> (functor, ranges);

    //sync so that we can check the results of the call.
    //In the future I want move this before the schedule call, and throwing
    //an exception if the previous schedule wrote an error. This would help
    //cuda to run longer before we hard sync.
    VTKM_CUDA_CALL(cudaDeviceSynchronize());

    //check what the value is
    if (hostErrorPtr[0] != '\0')
      {
      throw vtkm::cont::ErrorExecution(hostErrorPtr);
      }
  }

  template<typename T, class Storage>
  VTKM_CONT static void Sort(
      vtkm::cont::ArrayHandle<T,Storage>& values)
  {
    SortPortal(values.PrepareForInPlace(DeviceAdapterTag()));
  }

  template<typename T, class Storage, class BinaryCompare>
  VTKM_CONT static void Sort(
      vtkm::cont::ArrayHandle<T,Storage>& values,
      BinaryCompare binary_compare)
  {
    SortPortal(values.PrepareForInPlace(DeviceAdapterTag()),binary_compare);
  }

  template<typename T, typename U,
           class StorageT, class StorageU>
  VTKM_CONT static void SortByKey(
      vtkm::cont::ArrayHandle<T,StorageT>& keys,
      vtkm::cont::ArrayHandle<U,StorageU>& values)
  {
    SortByKeyPortal(keys.PrepareForInPlace(DeviceAdapterTag()),
                    values.PrepareForInPlace(DeviceAdapterTag()));
  }

  template<typename T, typename U,
           class StorageT, class StorageU,
           class BinaryCompare>
  VTKM_CONT static void SortByKey(
      vtkm::cont::ArrayHandle<T,StorageT>& keys,
      vtkm::cont::ArrayHandle<U,StorageU>& values,
      BinaryCompare binary_compare)
  {
    SortByKeyPortal(keys.PrepareForInPlace(DeviceAdapterTag()),
                    values.PrepareForInPlace(DeviceAdapterTag()),
                    binary_compare);
  }

  template<typename T, class Storage>
  VTKM_CONT static void Unique(
      vtkm::cont::ArrayHandle<T,Storage> &values)
  {
    vtkm::Id newSize = UniquePortal(values.PrepareForInPlace(DeviceAdapterTag()));

    values.Shrink(newSize);
  }

  template<typename T, class Storage, class BinaryCompare>
  VTKM_CONT static void Unique(
      vtkm::cont::ArrayHandle<T,Storage> &values,
      BinaryCompare binary_compare)
  {
    vtkm::Id newSize = UniquePortal(values.PrepareForInPlace(DeviceAdapterTag()),binary_compare);

    values.Shrink(newSize);
  }

  template<typename T, class SIn, class SVal, class SOut>
  VTKM_CONT static void UpperBounds(
      const vtkm::cont::ArrayHandle<T,SIn>& input,
      const vtkm::cont::ArrayHandle<T,SVal>& values,
      vtkm::cont::ArrayHandle<vtkm::Id,SOut>& output)
  {
    vtkm::Id numberOfValues = values.GetNumberOfValues();
    UpperBoundsPortal(input.PrepareForInput(DeviceAdapterTag()),
                      values.PrepareForInput(DeviceAdapterTag()),
                      output.PrepareForOutput(numberOfValues, DeviceAdapterTag()));
  }

  template<typename T, class SIn, class SVal, class SOut, class BinaryCompare>
  VTKM_CONT static void UpperBounds(
      const vtkm::cont::ArrayHandle<T,SIn>& input,
      const vtkm::cont::ArrayHandle<T,SVal>& values,
      vtkm::cont::ArrayHandle<vtkm::Id,SOut>& output,
      BinaryCompare binary_compare)
  {
    vtkm::Id numberOfValues = values.GetNumberOfValues();
    UpperBoundsPortal(input.PrepareForInput(DeviceAdapterTag()),
                      values.PrepareForInput(DeviceAdapterTag()),
                      output.PrepareForOutput(numberOfValues, DeviceAdapterTag()),
                      binary_compare);
  }

  template<class SIn, class SOut>
  VTKM_CONT static void UpperBounds(
      const vtkm::cont::ArrayHandle<vtkm::Id,SIn> &input,
      vtkm::cont::ArrayHandle<vtkm::Id,SOut> &values_output)
  {
    UpperBoundsPortal(input.PrepareForInput(DeviceAdapterTag()),
                      values_output.PrepareForInPlace(DeviceAdapterTag()));
  }
};

}
}
}
} // namespace vtkm::cont::cuda::internal

#endif //vtk_m_cont_cuda_internal_DeviceAdapterAlgorithmThrust_h
