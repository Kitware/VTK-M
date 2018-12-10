//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//
//  Copyright 2014 National Technology & Engineering Solutions of Sandia, LLC (NTESS).
//  Copyright 2014 UT-Battelle, LLC.
//  Copyright 2014 Los Alamos National Security.
//
//  Under the terms of Contract DE-NA0003525 with NTESS,
//  the U.S. Government retains certain rights in this software.
//
//  Under the terms of Contract DE-AC52-06NA25396 with Los Alamos National
//  Laboratory (LANL), the U.S. Government retains certain rights in
//  this software.
//============================================================================
#include <vtkm/cont/ArrayHandle.h>
#include <vtkm/cont/ArrayHandleVirtualCoordinates.h>

#include <vtkm/cont/CellSetExplicit.h>
#include <vtkm/cont/CellSetStructured.h>
#include <vtkm/cont/CoordinateSystem.h>
#include <vtkm/cont/DataSet.h>
#include <vtkm/cont/Field.h>

#include <vtkm/TypeListTag.h>
#include <vtkm/cont/testing/Testing.h>

#include <type_traits>

namespace
{

// clang-format off
template<typename T>
void is_noexcept_movable()
{
  constexpr bool valid = std::is_nothrow_move_constructible<T>::value &&
                         std::is_nothrow_move_assignable<T>::value;

  std::string msg = typeid(T).name() + std::string(" should be noexcept moveable");
  VTKM_TEST_ASSERT(valid, msg);
}

template<typename T>
void is_triv_noexcept_movable()
{
  constexpr bool valid =
#if !(defined(VTKM_GCC) && (__GNUC__ == 4 && __GNUC_MINOR__ <= 8))
                         //GCC 4.8 doesn't have implementations for is_trivially_*
                         std::is_trivially_move_constructible<T>::value &&
                         std::is_trivially_move_assignable<T>::value &&
#endif
                         std::is_nothrow_move_constructible<T>::value &&
                         std::is_nothrow_move_assignable<T>::value;

  std::string msg = typeid(T).name() + std::string(" should be noexcept moveable");
  VTKM_TEST_ASSERT(valid, msg);
}
// clang-format o

struct IsTrivNoExcept
{
  template <typename T>
  void operator()(T) const
  {
    is_triv_noexcept_movable<T>();
  }
};

struct IsNoExceptHandle
{
  template <typename T>
  void operator()(T) const
  {
    is_noexcept_movable< vtkm::cont::ArrayHandle<T> >();
  }
};

}

//-----------------------------------------------------------------------------
void TestContDataTypesHaveMoveSemantics()
{
  //verify the Vec types are triv and noexcept
  vtkm::testing::Testing::TryTypes( IsTrivNoExcept{}, vtkm::TypeListTagVecCommon{} );
  is_triv_noexcept_movable<vtkm::Vec<vtkm::Vec<float,3>,3>>();


  //verify that ArrayHandles are noexcept movable
  //allowing for efficient storage in containers such as std::vector
  vtkm::testing::Testing::TryTypes( IsNoExceptHandle{}, vtkm::TypeListTagAll{} );

  //verify the DataSet, Field, and CoordinateSystem,
  //all have efficient storage in containers such as std::vector
  is_noexcept_movable<vtkm::cont::DataSet>();
  is_noexcept_movable<vtkm::cont::Field>();
  is_noexcept_movable<vtkm::cont::CoordinateSystem>();


  //verify the CellSetStructured, and CellSetExplicit
  //have efficient storage in containers such as std::vector
  is_noexcept_movable<vtkm::cont::CellSetStructured<2>>();
  is_noexcept_movable<vtkm::cont::CellSetStructured<3>>();
  is_noexcept_movable<vtkm::cont::CellSetExplicit<>>();

}


//-----------------------------------------------------------------------------
int UnitTestMoveConstructors(int, char* [])
{
  return vtkm::cont::testing::Testing::Run(TestContDataTypesHaveMoveSemantics);
}
