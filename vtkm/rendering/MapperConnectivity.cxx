//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//
//  Copyright 2015 Sandia Corporation.
//  Copyright 2015 UT-Battelle, LLC.
//  Copyright 2015 Los Alamos National Security.
//
//  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
//  the U.S. Government retains certain rights in this software.
//
//  Under the terms of Contract DE-AC52-06NA25396 with Los Alamos National
//  Laboratory (LANL), the U.S. Government retains certain rights in
//  this software.
//============================================================================
#include <vtkm/rendering/CanvasRayTracer.h>
#include <vtkm/rendering/ColorTable.h>
#include <vtkm/rendering/ConnectivityProxy.h>
#include <vtkm/rendering/Mapper.h>
#include <vtkm/rendering/MapperConnectivity.h>
#include <vtkm/rendering/View.h>

#include <vtkm/rendering/raytracing/Camera.h>
#include <vtkm/rendering/raytracing/ConnectivityTracerFactory.h>

#include <cstdlib>

namespace vtkm
{
namespace rendering
{

VTKM_CONT
MapperConnectivity::MapperConnectivity()
{
  CanvasRT = NULL;
  SampleDistance = -1;
}

VTKM_CONT
MapperConnectivity::~MapperConnectivity()
{
}

VTKM_CONT
void MapperConnectivity::SetSampleDistance(const vtkm::Float32& distance)
{
  SampleDistance = distance;
}

VTKM_CONT
void MapperConnectivity::SetCanvas(Canvas* canvas)
{
  if (canvas != NULL)
  {

    CanvasRT = dynamic_cast<CanvasRayTracer*>(canvas);
    if (CanvasRT == NULL)
    {
      throw vtkm::cont::ErrorBadValue("Volume Render: bad canvas type. Must be CanvasRayTracer");
    }
  }
}

vtkm::rendering::Canvas* MapperConnectivity::GetCanvas() const
{
  return CanvasRT;
}


VTKM_CONT
void MapperConnectivity::RenderCells(const vtkm::cont::DynamicCellSet& cellset,
                                     const vtkm::cont::CoordinateSystem& coords,
                                     const vtkm::cont::Field& scalarField,
                                     const vtkm::rendering::ColorTable& vtkmNotUsed(colorTable),
                                     const vtkm::rendering::Camera& camera,
                                     const vtkm::Range& vtkmNotUsed(scalarRange))
{
  vtkm::rendering::ConnectivityProxy tracerProxy(cellset, coords, scalarField);
  if (SampleDistance != -1.f)
  {
    tracerProxy.SetSampleDistance(SampleDistance);
  }
  tracerProxy.SetColorMap(ColorMap);
  tracerProxy.Trace(camera, CanvasRT);
}

void MapperConnectivity::StartScene()
{
  // Nothing needs to be done.
}

void MapperConnectivity::EndScene()
{
  // Nothing needs to be done.
}

vtkm::rendering::Mapper* MapperConnectivity::NewCopy() const
{
  return new vtkm::rendering::MapperConnectivity(*this);
}
}
} // namespace vtkm::rendering