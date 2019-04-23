//=============================================================================
//
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
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
//  Under the terms of Contract DE-AC52-06NA25396 with Los Alamos National
//  Laboratory (LANL), the U.S. Government retains certain rights in
//  this software.
//
//=============================================================================

#ifndef vtk_m_worklet_moments_ComputeMoments_h
#define vtk_m_worklet_moments_ComputeMoments_h

#include <vtkm/worklet/DispatcherPointNeighborhood.h>
#include <vtkm/worklet/WorkletPointNeighborhood.h>

namespace vtkm
{
namespace worklet
{
namespace moments
{
struct ComputeMoments : public vtkm::worklet::WorkletPointNeighborhood
{
public:
  ComputeMoments(int _r, int _p, int _q)
    : Radius(_r)
    , p(_p)
    , q(_q)
  {
    assert(_r >= 0);
    assert(_p >= 0);
    assert(_q >= 0);
  }

  using ControlSignature = void(CellSetIn, FieldInNeighborhood, FieldOut);

  using ExecutionSignature = void(_2, _3);

  template <typename NeighIn, typename T>
  VTKM_EXEC void operator()(const NeighIn& image, T& moment) const
  {
    vtkm::Float64 sum = 0;
    vtkm::Float64 recp = 1.0 / Radius;

    for (int j = -Radius; j <= Radius; j++)
    {
      for (int i = -Radius; i <= Radius; i++)
      {
        if (i * i + j * j <= Radius * Radius)
          sum += pow(i * recp, p) * pow(j * recp, q) * image.Get(i, j, 0);
      }
    }

    moment = T(sum * recp * recp);
  }

private:
  const int Radius;
  const int p;
  const int q;
};
}
}
}

#endif // vtk_m_worklet_moments_ComputeMoments_h
