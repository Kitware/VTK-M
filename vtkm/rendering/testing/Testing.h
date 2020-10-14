//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
#ifndef vtk_m_rendering_testing_Testing_h
#define vtk_m_rendering_testing_Testing_h


#include <vtkm/cont/DataSet.h>
#include <vtkm/cont/Error.h>
#include <vtkm/cont/testing/Testing.h>

#include <vtkm/filter/ImageDifference.h>
#include <vtkm/internal/Configure.h>
#include <vtkm/io/ImageUtils.h>

#include <fstream>
#include <sstream>
#include <vector>

namespace vtkm
{
namespace rendering
{
namespace testing
{

/// \brief Tests multiple image files against a provided view pointer for differences
///
/// This function tests multiple files provided via fileNames against the rendered
/// canvas generated by the provided view using the ImageDifference filter. If one
/// of the provided images is within the error threshold for the image difference
/// this function will return true. Otherwise the view is too different from the images
/// and this will return false with corresponding error messages.
///
/// This function will generate an image if the provided file is missing. If a file is
/// missing the image will be generated for that file and the test will continue.
///
template <typename ViewType>
inline vtkm::cont::testing::TestEqualResult test_equal_images(
  const std::shared_ptr<ViewType> view,
  const std::vector<std::string>& fileNames,
  const vtkm::FloatDefault& threshold = 0.05f,
  const vtkm::IdComponent& radius = 0,
  const bool& average = false,
  const bool& writeDiff = true,
  const bool& returnOnPass = true)
{
  vtkm::cont::testing::TestEqualResult testResults;

  if (fileNames.empty())
  {
    testResults.PushMessage("No valid image file names were provided");
    return testResults;
  }

  view->Paint();
  view->GetCanvas().RefreshColorBuffer();
  const std::string testImageName =
    vtkm::cont::testing::Testing::WriteDirPath("test-" + fileNames[0]);
  vtkm::io::WriteImageFile(view->GetCanvas().GetDataSet(), testImageName, "color");

  for (const auto& fileName : fileNames)
  {
    VTKM_LOG_S(vtkm::cont::LogLevel::Info, "testing image file: " << fileName);
    vtkm::cont::testing::TestEqualResult imageResult;
    vtkm::cont::DataSet imageDataSet;

    try
    {
      const std::string testImagePath = vtkm::cont::testing::Testing::RegressionImagePath(fileName);
      imageDataSet = vtkm::io::ReadImageFile(testImagePath, "baseline-image");
    }
    catch (const vtkm::cont::ErrorExecution& error)
    {
      VTKM_LOG_S(vtkm::cont::LogLevel::Error, error.what());
      imageResult.PushMessage(error.GetMessage());

      const std::string outputImagePath = vtkm::cont::testing::Testing::WriteDirPath(fileName);
      vtkm::io::WriteImageFile(view->GetCanvas().GetDataSet(), outputImagePath, "color");

      imageResult.PushMessage("File '" + fileName + "' did not exist but has been generated");
      testResults.PushMessage(imageResult.GetMergedMessage());
      continue;
    }
    catch (const vtkm::cont::ErrorBadValue& error)
    {
      VTKM_LOG_S(vtkm::cont::LogLevel::Error, error.what());
      imageResult.PushMessage(error.GetMessage());
      imageResult.PushMessage("Unsupported file type for image: " + fileName);
      testResults.PushMessage(imageResult.GetMergedMessage());
      continue;
    }

    imageDataSet.AddPointField("generated-image", view->GetCanvas().GetColorBuffer());
    vtkm::filter::ImageDifference filter;
    filter.SetPrimaryField("baseline-image");
    filter.SetSecondaryField("generated-image");
    filter.SetThreshold(threshold);
    filter.SetRadius(radius);
    filter.SetAveragePixels(average);
    auto resultDataSet = filter.Execute(imageDataSet);

    if (!filter.GetImageDiffWithinThreshold())
    {
      imageResult.PushMessage("Image Difference was not within the expected threshold for: " +
                              fileName);
    }

    if (writeDiff && resultDataSet.HasPointField("image-diff"))
    {
      const std::string diffName = vtkm::cont::testing::Testing::WriteDirPath("diff-" + fileName);
      vtkm::io::WriteImageFile(resultDataSet, diffName, "image-diff");
    }

    if (imageResult && returnOnPass)
    {
      VTKM_LOG_S(vtkm::cont::LogLevel::Info, "Test passed for image " << fileName);
      if (!testResults)
      {
        VTKM_LOG_S(vtkm::cont::LogLevel::Info,
                   "Other image errors: " << testResults.GetMergedMessage());
      }
      return imageResult;
    }

    testResults.PushMessage(imageResult.GetMergedMessage());
  }

  return testResults;
}

template <typename ViewType>
inline vtkm::cont::testing::TestEqualResult test_equal_images(
  const std::shared_ptr<ViewType> view,
  const std::string& fileName,
  const vtkm::FloatDefault& threshold = 0.05f,
  const vtkm::IdComponent& radius = 0,
  const bool& average = false,
  const bool& writeDiff = true)
{
  std::vector<std::string> fileNames{ fileName };
  return test_equal_images(view, fileNames, threshold, radius, average, writeDiff);
}

/// \brief Tests multiple images in the format `fileName#.png`
///
/// Using the provided fileName, it splits the extension and prefix into two
/// components and searches through the regression image file path directory
/// for all matching file names with a number specifier starting at 0 incrementing
/// by one.
///
/// For example, if a file `foo.png` is provied, this function will first look
/// for a file foo0.png. If it exists, it will then look for foo1.png and so on
/// until it cannot find a file with a specific number.
///
/// test_equal_images will then be called on the vector of valid fileNames
///
template <typename ViewType>
inline vtkm::cont::testing::TestEqualResult test_equal_images_matching_name(
  const std::shared_ptr<ViewType> view,
  const std::string& fileName,
  const vtkm::FloatDefault& threshold = 0.05f,
  const vtkm::IdComponent& radius = 0,
  const bool& average = false,
  const bool& writeDiff = true,
  const bool& returnOnPass = true)
{
  std::vector<std::string> fileNames{ fileName };
  auto found = fileName.rfind(".");
  auto prefix = fileName.substr(0, found);
  auto suffix = fileName.substr(found, fileName.length());

  for (int i = 0;; i++)
  {
    std::ostringstream fileNameStream;
    fileNameStream << prefix << i << suffix;
    std::ifstream check(
      vtkm::cont::testing::Testing::RegressionImagePath(fileNameStream.str()).c_str());
    if (!check.good())
    {
      VTKM_LOG_S(vtkm::cont::LogLevel::Info,
                 "Could not find file with name " << fileNameStream.str() << ", beginning testing");
      break;
    }
    fileNames.emplace_back(fileNameStream.str());
  }
  test_equal_images(view, fileNames, threshold, radius, average, writeDiff, returnOnPass);
}


} // vtkm::rendering::testing
} // vtkm::rendering
} // vtkm

#endif // vtk_m_rendering_testing_Testing_h
