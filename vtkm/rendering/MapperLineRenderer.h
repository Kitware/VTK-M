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
#ifndef vtk_m_rendering_MapperLineRenderer_h
#define vtk_m_rendering_MapperLineRenderer_h

#include <vtkm/cont/CoordinateSystem.h>
#include <vtkm/cont/DynamicCellSet.h>
#include <vtkm/cont/Field.h>
#include <vtkm/rendering/Camera.h>
#include <vtkm/rendering/CanvasLineRenderer.h>
#include <vtkm/rendering/ColorTable.h>
#include <vtkm/rendering/Mapper.h>

namespace vtkm
{
namespace rendering
{

class VTKM_RENDERING_EXPORT MapperLineRenderer : public Mapper
{
public:
  VTKM_CONT
  MapperLineRenderer();
  virtual ~MapperLineRenderer();

  virtual void SetActiveColorTable(const ColorTable& ct) VTKM_OVERRIDE;
  virtual vtkm::rendering::Canvas* GetCanvas() const VTKM_OVERRIDE;
  virtual void SetCanvas(vtkm::rendering::Canvas* canvas) VTKM_OVERRIDE;

  virtual void StartScene() VTKM_OVERRIDE;
  virtual void EndScene() VTKM_OVERRIDE;
  virtual void RenderCells(const vtkm::cont::DynamicCellSet& cellset,
                           const vtkm::cont::CoordinateSystem& coords,
                           const vtkm::cont::Field& scalarField,
                           const vtkm::rendering::ColorTable& colorTable,
                           const vtkm::rendering::Camera& camera,
                           const vtkm::Range& scalarRange) VTKM_OVERRIDE;


  virtual vtkm::rendering::Mapper* NewCopy() const VTKM_OVERRIDE;

private:
  vtkm::rendering::CanvasLineRenderer* Canvas;
};
}
} //namespace vtkm::rendering
#endif //vtk_m_rendering_MapperLineRenderer_h
