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

#ifndef vtk_m_filter_Clip_h
#define vtk_m_filter_Clip_h

#include <vtkm/filter/FilterDataSetWithField.h>
#include <vtkm/worklet/Clip.h>

namespace vtkm {
namespace filter {

class Clip : public vtkm::filter::FilterDataSetWithField<Clip>
{
public:
  VTKM_CONT_EXPORT
  Clip();

  VTKM_CONT_EXPORT
  void SetClipValue(vtkm::Float64 value){ this->ClipValue = value; }

  VTKM_CONT_EXPORT
  vtkm::Float64 GetClipValue() const    { return this->ClipValue; }

  template<typename T, typename StorageType, typename DerivedPolicy, typename DeviceAdapter>
  VTKM_CONT_EXPORT
  vtkm::filter::ResultDataSet DoExecute(const vtkm::cont::DataSet& input,
                                        const vtkm::cont::ArrayHandle<T, StorageType>& field,
                                        const vtkm::filter::FieldMetadata& fieldMeta,
                                        const vtkm::filter::PolicyBase<DerivedPolicy>& policy,
                                        const DeviceAdapter& tag);

  //Map a new field onto the resulting dataset after running the filter.
  //This call is only valid after Execute has been called.
  template<typename T, typename StorageType, typename DerivedPolicy, typename DeviceAdapter>
  VTKM_CONT_EXPORT
  bool DoMapField(vtkm::filter::ResultDataSet& result,
                  const vtkm::cont::ArrayHandle<T, StorageType>& input,
                  const vtkm::filter::FieldMetadata& fieldMeta,
                  const vtkm::filter::PolicyBase<DerivedPolicy>& policy,
                  const DeviceAdapter& tag);


private:
  vtkm::Float64 ClipValue;
  vtkm::worklet::Clip Worklet;
};

template<>
class FilterTraits<Clip>
{ //currently the Clip filter only works on scalar data.
public:
  typedef TypeListTagScalarAll InputFieldTypeList;
};


}
} // namespace vtkm::filter


#include <vtkm/filter/Clip.hxx>

#endif // vtk_m_filter_Clip_h