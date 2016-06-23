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
//  Copyright 2016 Sandia Corporation.
//  Copyright 2016 UT-Battelle, LLC.
//  Copyright 2016 Los Alamos National Security.
//
//  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
//  the U.S. Government retains certain rights in this software.
//  Under the terms of Contract DE-AC52-06NA25396 with Los Alamos National
//  Laboratory (LANL), the U.S. Government retains certain rights in
//  this software.
//
//=============================================================================

#include <vtkm/Transform3D.h>

#include <vtkm/testing/Testing.h>

VTKM_THIRDPARTY_PRE_INCLUDE
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_real_distribution.hpp>
VTKM_THIRDPARTY_POST_INCLUDE

namespace {

boost::mt19937 g_RandomGenerator;

template<typename T>
struct TransformTests
{
  boost::random::uniform_real_distribution<T> RandomDistribution;
  TransformTests()
    : RandomDistribution(0.0f, 1.0f) {  }

  T RandomNum() const { return this->RandomDistribution(g_RandomGenerator); }

  typedef vtkm::Vec<T,3> Vec;
  typedef vtkm::Matrix<T,4,4> Transform;

  Vec RandomVector() const
  {
    Vec vec(this->RandomNum(), this->RandomNum(), this->RandomNum());
    return T(2)*vec - Vec(1);
  }

  void CheckTranslate() const
  {
    std::cout << "--- Checking translate" << std::endl;

    Vec startPoint = this->RandomVector();
    std::cout << " Starting point: " << startPoint << std::endl;

    Vec translateAmount = this->RandomVector();
    std::cout << " Translation amount: " << translateAmount << std::endl;

    Transform translate = vtkm::Transform3DTranslate(translateAmount);

    Vec translated1 = vtkm::Transform3DPoint(translate, startPoint);
    std::cout << " First translation: " << translated1 << std::endl;
    VTKM_TEST_ASSERT(test_equal(translated1, startPoint+translateAmount),
                     "Bad translation.");

    Vec translated2 = vtkm::Transform3DPoint(translate, translated1);
    std::cout << " Second translation: " << translated2 << std::endl;
    VTKM_TEST_ASSERT(test_equal(translated2, startPoint+T(2)*translateAmount),
                     "Bad translation.");

    // Vectors should be invarient to translation.
    translated1 = vtkm::Transform3DVector(translate, startPoint);
    std::cout << " Translated vector: " << translated1 << std::endl;
    VTKM_TEST_ASSERT(test_equal(translated1, startPoint), "Bad translation.");
  }

  void CheckScale() const
  {
    std::cout << "--- Checking scale" << std::endl;

    Vec startPoint = this->RandomVector();
    std::cout << " Starting point: " << startPoint << std::endl;

    Vec scaleAmount = this->RandomVector();
    std::cout << " Scale amount: " << scaleAmount << std::endl;

    Transform scale = vtkm::Transform3DScale(scaleAmount);

    Vec scaled1 = vtkm::Transform3DPoint(scale, startPoint);
    std::cout << " First scale: " << scaled1 << std::endl;
    VTKM_TEST_ASSERT(test_equal(scaled1, startPoint*scaleAmount),
                     "Bad scale.");

    Vec scaled2 = vtkm::Transform3DPoint(scale, scaled1);
    std::cout << " Second scale: " << scaled2 << std::endl;
    VTKM_TEST_ASSERT(test_equal(scaled2, startPoint*scaleAmount*scaleAmount),
                     "Bad scale.");

    // Vectors should scale the same as points.
    scaled1 = vtkm::Transform3DVector(scale, startPoint);
    std::cout << " Scaled vector: " << scaled1 << std::endl;
    VTKM_TEST_ASSERT(test_equal(scaled1, startPoint*scaleAmount),
                     "Bad scale.");
  }

  void CheckRotate() const
  {
    std::cout << "--- Checking rotate" << std::endl;

    Vec startPoint = this->RandomVector();
    std::cout << " Starting point: " << startPoint << std::endl;

    const T ninetyDegrees = T(90);

    std::cout << "--Rotate 90 degrees around X" << std::endl;
    Transform rotateX = vtkm::Transform3DRotateX(ninetyDegrees);

    Vec rotated1 = vtkm::Transform3DPoint(rotateX, startPoint);
    std::cout << " First rotate: " << rotated1 << std::endl;
    VTKM_TEST_ASSERT(
          test_equal(rotated1, Vec(startPoint[0],-startPoint[2],startPoint[1])),
          "Bad rotate.");

    Vec rotated2 = vtkm::Transform3DPoint(rotateX, rotated1);
    std::cout << " Second rotate: " << rotated2 << std::endl;
    VTKM_TEST_ASSERT(
          test_equal(rotated2, Vec(startPoint[0],-startPoint[1],-startPoint[2])),
          "Bad rotate.");

    // Vectors should rotate the same as points.
    rotated1 = vtkm::Transform3DVector(rotateX, startPoint);
    std::cout << " Vector rotate: " << rotated1 << std::endl;
    VTKM_TEST_ASSERT(
          test_equal(rotated1, Vec(startPoint[0],-startPoint[2],startPoint[1])),
          "Bad rotate.");

    std::cout << "--Rotate 90 degrees around Y" << std::endl;
    Transform rotateY = vtkm::Transform3DRotateY(ninetyDegrees);

    rotated1 = vtkm::Transform3DPoint(rotateY, startPoint);
    std::cout << " First rotate: " << rotated1 << std::endl;
    VTKM_TEST_ASSERT(
          test_equal(rotated1, Vec(startPoint[2],startPoint[1],-startPoint[0])),
          "Bad rotate.");

    rotated2 = vtkm::Transform3DPoint(rotateY, rotated1);
    std::cout << " Second rotate: " << rotated2 << std::endl;
    VTKM_TEST_ASSERT(
          test_equal(rotated2, Vec(-startPoint[0],startPoint[1],-startPoint[2])),
          "Bad rotate.");

    // Vectors should rotate the same as points.
    rotated1 = vtkm::Transform3DVector(rotateY, startPoint);
    std::cout << " Vector rotate: " << rotated1 << std::endl;
    VTKM_TEST_ASSERT(
          test_equal(rotated1, Vec(startPoint[2],startPoint[1],-startPoint[0])),
          "Bad rotate.");

    std::cout << "--Rotate 90 degrees around Z" << std::endl;
    Transform rotateZ = vtkm::Transform3DRotateZ(ninetyDegrees);

    rotated1 = vtkm::Transform3DPoint(rotateZ, startPoint);
    std::cout << " First rotate: " << rotated1 << std::endl;
    VTKM_TEST_ASSERT(
          test_equal(rotated1, Vec(-startPoint[1],startPoint[0],startPoint[2])),
          "Bad rotate.");

    rotated2 = vtkm::Transform3DPoint(rotateZ, rotated1);
    std::cout << " Second rotate: " << rotated2 << std::endl;
    VTKM_TEST_ASSERT(
          test_equal(rotated2, Vec(-startPoint[0],-startPoint[1],startPoint[2])),
          "Bad rotate.");

    // Vectors should rotate the same as points.
    rotated1 = vtkm::Transform3DVector(rotateZ, startPoint);
    std::cout << " Vector rotate: " << rotated1 << std::endl;
    VTKM_TEST_ASSERT(
          test_equal(rotated1, Vec(-startPoint[1],startPoint[0],startPoint[2])),
          "Bad rotate.");
  }
};

struct TryTransformsFunctor
{
  template<typename T>
  void operator()(T) const
  {
    TransformTests<T> tests;
    tests.CheckTranslate();
    tests.CheckScale();
    tests.CheckRotate();
  }
};

void TestTransforms()
{
  vtkm::UInt32 seed = static_cast<vtkm::UInt32>(time(NULL));
  std::cout << "Seed: " << seed << std::endl;
  g_RandomGenerator.seed(seed);

  vtkm::testing::Testing::TryTypes(TryTransformsFunctor(),
                                   vtkm::TypeListTagFieldScalar());
}

} // anonymous namespace

int UnitTestTransform3D(int, char *[])
{
  return vtkm::testing::Testing::Run(TestTransforms);
}