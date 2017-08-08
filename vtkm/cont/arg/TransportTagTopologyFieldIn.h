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
#ifndef vtk_m_cont_arg_TransportTagTopologyFieldIn_h
#define vtk_m_cont_arg_TransportTagTopologyFieldIn_h

#include <vtkm/TopologyElementTag.h>
#include <vtkm/Types.h>

#include <vtkm/cont/ArrayHandle.h>
#include <vtkm/cont/CellSet.h>

#include <vtkm/cont/arg/Transport.h>

namespace vtkm
{
namespace cont
{
namespace arg
{

/// \brief \c Transport tag for input arrays in topology maps.
///
/// \c TransportTagTopologyFieldIn is a tag used with the \c Transport class to
/// transport \c ArrayHandle objects for input data. The transport is templated
/// on a topology element tag and expects a cell set input domain to check the
/// size of the input array.
///
template <typename TopologyElementTag>
struct TransportTagTopologyFieldIn
{
};

namespace detail
{

VTKM_CONT
inline static vtkm::Id TopologyDomainSize(const vtkm::cont::CellSet& cellSet,
                                          vtkm::TopologyElementTagPoint)
{
  return cellSet.GetNumberOfPoints();
}

VTKM_CONT
inline static vtkm::Id TopologyDomainSize(const vtkm::cont::CellSet& cellSet,
                                          vtkm::TopologyElementTagCell)
{
  return cellSet.GetNumberOfCells();
}

VTKM_CONT
inline static vtkm::Id TopologyDomainSize(const vtkm::cont::CellSet& cellSet,
                                          vtkm::TopologyElementTagFace)
{
  return cellSet.GetNumberOfFaces();
}

VTKM_CONT
inline static vtkm::Id TopologyDomainSize(const vtkm::cont::CellSet& cellSet,
                                          vtkm::TopologyElementTagEdge)
{
  return cellSet.GetNumberOfEdges();
}

} // namespace detail

template <typename TopologyElementTag, typename ContObjectType, typename Device>
struct Transport<vtkm::cont::arg::TransportTagTopologyFieldIn<TopologyElementTag>,
                 ContObjectType,
                 Device>
{
  VTKM_IS_ARRAY_HANDLE(ContObjectType);

  using ExecObjectType = typename ContObjectType::template ExecutionTypes<Device>::PortalConst;

  VTKM_CONT
  ExecObjectType operator()(const ContObjectType& object,
                            const vtkm::cont::CellSet& inputDomain,
                            vtkm::Id,
                            vtkm::Id) const
  {
    if (object.GetNumberOfValues() != detail::TopologyDomainSize(inputDomain, TopologyElementTag()))
    {
      throw vtkm::cont::ErrorBadValue("Input array to worklet invocation the wrong size.");
    }

    return object.PrepareForInput(Device());
  }
};
}
}
} // namespace vtkm::cont::arg

#endif //vtk_m_cont_arg_TransportTagTopologyFieldIn_h
