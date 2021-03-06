//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef vtk_m_io_VTKRectilinearGridReader_h
#define vtk_m_io_VTKRectilinearGridReader_h

#include <vtkm/io/VTKDataSetReaderBase.h>

namespace vtkm
{
namespace io
{

class VTKM_IO_EXPORT VTKRectilinearGridReader : public VTKDataSetReaderBase
{
public:
  explicit VTKM_CONT VTKRectilinearGridReader(const char* fileName);
  explicit VTKM_CONT VTKRectilinearGridReader(const std::string& fileName);

private:
  VTKM_CONT void Read() override;
};
}
} // namespace vtkm::io

#endif // vtk_m_io_VTKRectilinearGridReader_h
