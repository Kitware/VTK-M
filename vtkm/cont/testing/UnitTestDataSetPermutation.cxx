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

#include <vtkm/cont/DataSet.h>
#include <vtkm/cont/CellSetSingleType.h>
#include <vtkm/cont/CellSetPermutation.h>
#include <vtkm/cont/DeviceAdapterAlgorithm.h>

#include <vtkm/cont/testing/Testing.h>
#include <vtkm/cont/testing/MakeTestDataSet.h>

#include <vtkm/worklet/CellAverage.h>
#include <vtkm/worklet/DispatcherMapTopology.h>

namespace {

template<typename T, typename Storage>
bool TestArrayHandle(const vtkm::cont::ArrayHandle<T, Storage> &ah, const T *expected,
                     vtkm::Id size)
{
  if (size != ah.GetNumberOfValues())
  {
    return false;
  }

  for (vtkm::Id i = 0; i < size; ++i)
  {
    if (ah.GetPortalConstControl().Get(i) != expected[i])
    {
      return false;
    }
  }

  return true;
}

inline vtkm::cont::DataSet make_SingleTypeDataSet()
{
  using vtkm::cont::Field;

  vtkm::cont::DataSet dataSet;

  const int nVerts = 5;
  typedef vtkm::Vec<vtkm::Float32,3> CoordType;
  CoordType coordinates[nVerts] = {
    CoordType(0, 0, 0),
    CoordType(1, 0, 0),
    CoordType(1, 1, 0),
    CoordType(2, 1, 0),
    CoordType(2, 2, 0)
  };

  //Set coordinate system
  dataSet.AddCoordinateSystem(
        vtkm::cont::CoordinateSystem("coordinates", 1, coordinates, nVerts));

  //Set point scalar
  vtkm::Float32 vars[nVerts] = {10.1f, 20.1f, 30.2f, 40.2f, 50.3f};
  dataSet.AddField(Field("pointvar", 1, vtkm::cont::Field::ASSOC_POINTS, vars, nVerts));

  std::vector<vtkm::Id> conn;
  // First Cell
  conn.push_back(0);
  conn.push_back(1);
  conn.push_back(2);
  // Second Cell
  conn.push_back(1);
  conn.push_back(2);
  conn.push_back(3);
  // Third Cell
  conn.push_back(2);
  conn.push_back(3);
  conn.push_back(4);

  vtkm::cont::CellSetSingleType<> cellSet(vtkm::CellShapeTagTriangle(),
                                          "cells");
  cellSet.FillViaCopy(conn);

  dataSet.AddCellSet(cellSet);

  return dataSet;
}

void TestDataSet_Explicit()
{

  vtkm::cont::DataSet dataSet = make_SingleTypeDataSet();

  std::vector<vtkm::Id> validIds;
  validIds.push_back(1); //iterate the 2nd cell 4 times
  validIds.push_back(1);
  validIds.push_back(1);
  validIds.push_back(1);
  vtkm::cont::ArrayHandle<vtkm::Id> validCellIds =
                                      vtkm::cont::make_ArrayHandle(validIds);

  //get the cellset single type from the dataset
  typedef vtkm::cont::CellSetSingleType<> CellSetType;
  CellSetType cellSet = dataSet.GetCellSet(0).CastTo<CellSetType>();

  //verify that we can create a subset of a singlset
  typedef vtkm::cont::CellSetPermutation<vtkm::cont::ArrayHandle<vtkm::Id>,
                                    vtkm::cont::CellSetSingleType<> > SubsetType;
  SubsetType subset;
  subset.Fill(validCellIds,cellSet);

  subset.PrintSummary(std::cout);

  typedef SubsetType::ExecutionTypes<vtkm::cont::DeviceAdapterTagSerial,
                                     vtkm::TopologyElementTagPoint,
                                     vtkm::TopologyElementTagCell>::ExecObjectType ExecObjectType;

  ExecObjectType execConnectivity;
  execConnectivity = subset.PrepareForInput(vtkm::cont::DeviceAdapterTagSerial(),
                                            vtkm::TopologyElementTagPoint(),
                                            vtkm::TopologyElementTagCell());

  //run a basic for-each topology algorithm on this
  vtkm::cont::ArrayHandle<vtkm::Float32> result;
  vtkm::worklet::DispatcherMapTopology<vtkm::worklet::CellAverage> dispatcher;
  dispatcher.Invoke(dataSet.GetField("pointvar").GetData(),
                    subset,
                    result);

  //iterate same cell 4 times
  vtkm::Float32 expected[4] = { 30.1667f, 30.1667f, 30.1667f, 30.1667f };
  for (int i = 0; i < 4; ++i)
  {
    VTKM_TEST_ASSERT(test_equal(result.GetPortalConstControl().Get(i),
        expected[i]), "Wrong result for CellAverage worklet on explicit subset data");
  }
}

void TestDataSet_Structured2D()
{

  vtkm::cont::testing::MakeTestDataSet testDataSet;
  vtkm::cont::DataSet dataSet = testDataSet.Make2DRegularDataSet0();

  std::vector<vtkm::Id> validIds;
  validIds.push_back(1); //iterate the 2nd cell 4 times
  validIds.push_back(1);
  validIds.push_back(1);
  validIds.push_back(1);
  vtkm::cont::ArrayHandle<vtkm::Id> validCellIds =
                                      vtkm::cont::make_ArrayHandle(validIds);


  typedef vtkm::cont::CellSetStructured<2> CellSetType;
  CellSetType cellSet = dataSet.GetCellSet(0).CastTo<CellSetType>();

  //verify that we can create a subset of a 2d RegularDataSet
  vtkm::cont::CellSetPermutation<vtkm::cont::ArrayHandle<vtkm::Id>,
                            vtkm::cont::CellSetStructured<2> > subset;
  subset.Fill(validCellIds,cellSet);

  subset.PrintSummary(std::cout);

  //verify that we can call PrepareForInput on CellSetSingleType
  typedef vtkm::cont::DeviceAdapterTagSerial DeviceAdapterTag;

  //verify that PrepareForInput exists
  subset.PrepareForInput(DeviceAdapterTag(),
                          vtkm::TopologyElementTagPoint(),
                          vtkm::TopologyElementTagCell());

  //run a basic for-each topology algorithm on this
  vtkm::cont::ArrayHandle<vtkm::Float32> result;
  vtkm::worklet::DispatcherMapTopology<vtkm::worklet::CellAverage> dispatcher;
  dispatcher.Invoke(dataSet.GetField("pointvar").GetData(),
                    subset,
                    result);

  vtkm::Float32 expected[4] = { 40.1f, 40.1f, 40.1f, 40.1f };
  for (int i = 0; i < 4; ++i)
  {
    VTKM_TEST_ASSERT(test_equal(result.GetPortalConstControl().Get(i),
        expected[i]), "Wrong result for CellAverage worklet on 2d structured subset data");
  }
}

void TestDataSet_Structured3D()
{

  vtkm::cont::testing::MakeTestDataSet testDataSet;
  vtkm::cont::DataSet dataSet = testDataSet.Make3DRegularDataSet0();

  std::vector<vtkm::Id> validIds;
  validIds.push_back(1); //iterate the 2nd cell 4 times
  validIds.push_back(1);
  validIds.push_back(1);
  validIds.push_back(1);
  vtkm::cont::ArrayHandle<vtkm::Id> validCellIds =
                                      vtkm::cont::make_ArrayHandle(validIds);

  typedef vtkm::cont::CellSetStructured<3> CellSetType;
  CellSetType cellSet = dataSet.GetCellSet(0).CastTo<CellSetType>();

  //verify that we can create a subset of a 2d RegularDataSet
  vtkm::cont::CellSetPermutation<vtkm::cont::ArrayHandle<vtkm::Id>,
                            vtkm::cont::CellSetStructured<3> > subset;
  subset.Fill(validCellIds,cellSet);

  subset.PrintSummary(std::cout);


  //verify that PrepareForInput exists
  subset.PrepareForInput(
        vtkm::cont::DeviceAdapterTagSerial(),
        vtkm::TopologyElementTagPoint(),
        vtkm::TopologyElementTagCell());

  //run a basic for-each topology algorithm on this
  vtkm::cont::ArrayHandle<vtkm::Float32> result;
  vtkm::worklet::DispatcherMapTopology<vtkm::worklet::CellAverage> dispatcher;
  dispatcher.Invoke(dataSet.GetField("pointvar").GetData(),
                    subset,
                    result);

vtkm::Float32 expected[4] = { 70.2125f, 70.2125f, 70.2125f, 70.2125f };
  for (int i = 0; i < 4; ++i)
  {
    VTKM_TEST_ASSERT(test_equal(result.GetPortalConstControl().Get(i),
        expected[i]), "Wrong result for CellAverage worklet on 2d structured subset data");
  }

}

void TestDataSet_Permutation()
{
  std::cout << std::endl;
  std::cout << "--TestDataSet_Permutation--" << std::endl << std::endl;

  TestDataSet_Explicit();
  TestDataSet_Structured2D();
  TestDataSet_Structured3D();
}

}


int UnitTestDataSetPermutation(int, char *[])
{
  return vtkm::cont::testing::Testing::Run(TestDataSet_Permutation);
}