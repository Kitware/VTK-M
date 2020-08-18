//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================

#ifndef vtk_m_cont_ParticleArrayCopy_hxx
#define vtk_m_cont_ParticleArrayCopy_hxx

#include <vtkm/cont/Algorithm.h>
#include <vtkm/cont/ArrayCopy.h>
#include <vtkm/cont/ArrayHandleTransform.h>
#include <vtkm/cont/Invoker.h>
#include <vtkm/cont/ParticleArrayCopy.h>
#include <vtkm/worklet/WorkletMapField.h>

namespace vtkm
{
namespace cont
{

namespace detail
{

struct ExtractPositionFunctor
{
  VTKM_EXEC_CONT
  vtkm::Vec3f operator()(const vtkm::Massless& p) const { return p.Pos; }
};

struct ExtractTerminatedFunctor
{
  VTKM_EXEC_CONT
  bool operator()(const vtkm::Massless& p) const { return p.Status.CheckTerminate(); }
};

struct CopyParticleAllWorklet : public vtkm::worklet::WorkletMapField
{
  using ControlSignature = void(FieldIn inParticle,
                                FieldOut outPos,
                                FieldOut outID,
                                FieldOut outSteps,
                                FieldOut outStatus,
                                FieldOut outTime);

  VTKM_EXEC void operator()(const vtkm::Particle& inParticle,
                            vtkm::Vec3f& outPos,
                            vtkm::Id& outID,
                            vtkm::Id& outSteps,
                            vtkm::ParticleStatus& outStatus,
                            vtkm::FloatDefault& outTime) const
  {
    outPos = inParticle.Pos;
    outID = inParticle.ID;
    outSteps = inParticle.NumSteps;
    outStatus = inParticle.Status;
    outTime = inParticle.Time;
  }
};

} // namespace detail


template <typename ParticleType>
VTKM_ALWAYS_EXPORT inline void ParticleArrayCopy(
  const vtkm::cont::ArrayHandle<ParticleType, vtkm::cont::StorageTagBasic>& inP,
  vtkm::cont::ArrayHandle<vtkm::Vec3f, vtkm::cont::StorageTagBasic>& outPos,
  bool CopyTerminatedOnly)
{
  auto posTrn = vtkm::cont::make_ArrayHandleTransform(inP, detail::ExtractPositionFunctor());

  if (CopyTerminatedOnly)
  {
    auto termTrn = vtkm::cont::make_ArrayHandleTransform(inP, detail::ExtractTerminatedFunctor());
    vtkm::cont::Algorithm::CopyIf(posTrn, termTrn, outPos);
  }
  else
    vtkm::cont::ArrayCopy(posTrn, outPos);
}

/// \brief Copy all fields in vtkm::Particle to standard types.
///
/// Given an \c ArrayHandle of vtkm::Particle, this function copies the
/// position, ID, number of steps, status and time into a separate
/// \c ArrayHandle.
///

template <typename ParticleType>
VTKM_ALWAYS_EXPORT inline void ParticleArrayCopy(
  const vtkm::cont::ArrayHandle<ParticleType, vtkm::cont::StorageTagBasic>& inP,
  vtkm::cont::ArrayHandle<vtkm::Vec3f, vtkm::cont::StorageTagBasic>& outPos,
  vtkm::cont::ArrayHandle<vtkm::Id, vtkm::cont::StorageTagBasic>& outID,
  vtkm::cont::ArrayHandle<vtkm::Id, vtkm::cont::StorageTagBasic>& outSteps,
  vtkm::cont::ArrayHandle<vtkm::ParticleStatus, vtkm::cont::StorageTagBasic>& outStatus,
  vtkm::cont::ArrayHandle<vtkm::FloatDefault, vtkm::cont::StorageTagBasic>& outTime)
{
  vtkm::cont::Invoker invoke;
  detail::CopyParticleAllWorklet worklet;

  invoke(worklet, inP, outPos, outID, outSteps, outStatus, outTime);
}
}
} // namespace vtkm::cont

#endif //vtk_m_cont_ParticleArrayCopy_hxx