//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//
//  Copyright 2018 National Technology & Engineering Solutions of Sandia, LLC (NTESS).
//  Copyright 2018 UT-Battelle, LLC.
//  Copyright 2018 Los Alamos National Security.
//
//  Under the terms of Contract DE-NA0003525 with NTESS,
//  the U.S. Government retains certain rights in this software.
//
//  Under the terms of Contract DE-AC52-06NA25396 with Los Alamos National
//  Laboratory (LANL), the U.S. Government retains certain rights in
//  this software.
//============================================================================

#ifndef vtk_m_cont_internal_DeviceAdapterAtomicArrayImplementationCuda_h
#define vtk_m_cont_internal_DeviceAdapterAtomicArrayImplementationCuda_h

#include <vtkm/Types.h>

#include <vtkm/cont/ArrayHandle.h>
#include <vtkm/cont/DeviceAdapterAlgorithm.h>
#include <vtkm/cont/StorageBasic.h>

#include <vtkm/cont/cuda/internal/DeviceAdapterTagCuda.h>

// Disable warnings we check vtkm for but Thrust does not.
VTKM_THIRDPARTY_PRE_INCLUDE
#include <thrust/device_ptr.h>
VTKM_THIRDPARTY_POST_INCLUDE

namespace vtkm
{
namespace cont
{

/// CUDA contains its own atomic operations
///
template <typename T>
class DeviceAdapterAtomicArrayImplementation<T, vtkm::cont::DeviceAdapterTagCuda>
{
public:
  VTKM_CONT
  DeviceAdapterAtomicArrayImplementation(
    vtkm::cont::ArrayHandle<T, vtkm::cont::StorageTagBasic> handle)
    : Portal(handle.PrepareForInPlace(vtkm::cont::DeviceAdapterTagCuda()))
  {
  }

  VTKM_EXEC T Add(vtkm::Id index, const T& value) const
  {
    T* lockedValue = ::thrust::raw_pointer_cast(this->Portal.GetIteratorBegin() + index);
    return this->vtkmAtomicAdd(lockedValue, value);
  }

  VTKM_EXEC T CompareAndSwap(vtkm::Id index,
                             const vtkm::Int64& newValue,
                             const vtkm::Int64& oldValue) const
  {
    T* lockedValue = ::thrust::raw_pointer_cast(this->Portal.GetIteratorBegin() + index);
    return this->vtkmCompareAndSwap(lockedValue, newValue, oldValue);
  }

private:
  using PortalType =
    typename vtkm::cont::ArrayHandle<T, vtkm::cont::StorageTagBasic>::template ExecutionTypes<
      vtkm::cont::DeviceAdapterTagCuda>::Portal;
  PortalType Portal;

  __device__ vtkm::Int64 vtkmAtomicAdd(vtkm::Int64* address, const vtkm::Int64& value) const
  {
    return atomicAdd((unsigned long long*)address, (unsigned long long)value);
  }

  __device__ vtkm::Int32 vtkmAtomicAdd(vtkm::Int32* address, const vtkm::Int32& value) const
  {
    return atomicAdd(address, value);
  }

  __device__ vtkm::Int32 vtkmCompareAndSwap(vtkm::Int32* address,
                                            const vtkm::Int32& newValue,
                                            const vtkm::Int32& oldValue) const
  {
    return atomicCAS(address, oldValue, newValue);
  }

  __device__ vtkm::Int64 vtkmCompareAndSwap(vtkm::Int64* address,
                                            const vtkm::Int64& newValue,
                                            const vtkm::Int64& oldValue) const
  {
    return atomicCAS((unsigned long long int*)address,
                     (unsigned long long int)oldValue,
                     (unsigned long long int)newValue);
  }
};
}
} // end namespace vtkm::cont

#endif // vtk_m_cont_internal_DeviceAdapterAtomicArrayImplementationCuda_h