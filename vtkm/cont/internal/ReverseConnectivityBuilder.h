//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
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
//
//  Under the terms of Contract DE-AC52-06NA25396 with Los Alamos National
//  Laboratory (LANL), the U.S. Government retains certain rights in
//  this software.
//============================================================================
#ifndef vtk_m_cont_internal_ReverseConnectivityBuilder_h
#define vtk_m_cont_internal_ReverseConnectivityBuilder_h

#include <vtkm/cont/ArrayHandle.h>
#include <vtkm/cont/ArrayHandleCast.h>
#include <vtkm/cont/ArrayHandleConstant.h>
#include <vtkm/cont/DeviceAdapterAlgorithm.h>

#include <vtkm/exec/AtomicArray.h>
#include <vtkm/exec/FunctorBase.h>

#include <utility>

namespace vtkm
{
namespace cont
{
namespace internal
{

/// Takes a connectivity array handle (conn) and constructs a reverse
/// connectivity table suitable for use by VTK-m (rconn).
///
/// This code is generalized for use by VTK and VTK-m.
///
/// The Run(...) method is the main entry point. The template parameters are:
/// @param RConnToConnIdxCalc defines `vtkm::Id operator()(vtkm::Id in) const`
/// which computes the index of the in'th point id in conn. This is necessary
/// for VTK-style cell arrays that need to skip the cell length entries. In
/// vtk-m, this is a no-op passthrough.
/// @param ConnIdxToCellIdxCalc Functor that computes the cell id from an
/// index into conn.
/// @param ConnTag is the StorageTag for the input connectivity array.
///
/// See usages in vtkmCellSetExplicit and vtkmCellSetSingleType for examples.
template <typename RConnToConnIdxCalc,
          typename ConnIdxToCellIdxCalc,
          typename ConnTag,
          typename Device>
class ReverseConnectivityBuilder
{
public:
  using AtomicHistogram = vtkm::exec::AtomicArray<vtkm::IdComponent, Device>;
  using ConnArray = vtkm::cont::ArrayHandle<vtkm::Id, ConnTag>;
  using ConnInPortal = decltype(std::declval<ConnArray>().PrepareForInput(Device()));
  using IdArray = vtkm::cont::ArrayHandle<vtkm::Id>;
  using IdComponentArray = vtkm::cont::ArrayHandle<vtkm::IdComponent>;
  using ROffsetInPortal = decltype(std::declval<IdArray>().PrepareForInput(Device()));
  using RConnOutPortal = decltype(std::declval<IdArray>().PrepareForOutput(0, Device()));

  struct BuildHistogram : public vtkm::exec::FunctorBase
  {
    AtomicHistogram Histo;
    ConnInPortal Conn;
    RConnToConnIdxCalc IdxCalc;

    VTKM_CONT
    BuildHistogram(const AtomicHistogram& histo,
                   const ConnInPortal& conn,
                   const RConnToConnIdxCalc& idxCalc)
      : Histo(histo)
      , Conn(conn)
      , IdxCalc(idxCalc)
    {
    }

    VTKM_EXEC
    void operator()(vtkm::Id rconnIdx) const
    {
      // Compute the connectivity array index (skipping cell length entries)
      const vtkm::Id connIdx = this->IdxCalc(rconnIdx);
      const vtkm::Id ptId = this->Conn.Get(connIdx);
      this->Histo.Add(ptId, 1);
    }
  };

  struct GenerateRConn : public vtkm::exec::FunctorBase
  {
    AtomicHistogram Histo;
    ConnInPortal Conn;
    ROffsetInPortal ROffsets;
    RConnOutPortal RConn;
    RConnToConnIdxCalc IdxCalc;
    ConnIdxToCellIdxCalc CellIdCalc;

    VTKM_CONT
    GenerateRConn(const AtomicHistogram& histo,
                  const ConnInPortal& conn,
                  const ROffsetInPortal& rOffsets,
                  const RConnOutPortal& rconn,
                  const RConnToConnIdxCalc& idxCalc,
                  const ConnIdxToCellIdxCalc& cellIdCalc)
      : Histo(histo)
      , Conn(conn)
      , ROffsets(rOffsets)
      , RConn(rconn)
      , IdxCalc(idxCalc)
      , CellIdCalc(cellIdCalc)
    {
    }

    VTKM_EXEC
    void operator()(vtkm::Id inputIdx) const
    {
      // Compute the connectivity array index (skipping cell length entries)
      const vtkm::Id connIdx = this->IdxCalc(inputIdx);
      const vtkm::Id ptId = this->Conn.Get(connIdx);

      // Compute the cell id:
      const vtkm::Id cellId = this->CellIdCalc(connIdx);

      // Find the base offset for this point id:
      const vtkm::Id baseOffset = this->ROffsets.Get(ptId);

      // Find the next unused index for this point id
      const vtkm::Id nextAvailable = this->Histo.Add(ptId, 1);

      // Update the final location in the RConn table with the cellId
      const vtkm::Id rconnIdx = baseOffset + nextAvailable;
      this->RConn.Set(rconnIdx, cellId);
    }
  };

  VTKM_CONT
  static void Run(const ConnArray& conn,
                  IdArray& rConn,
                  IdComponentArray& rNumIndices,
                  IdArray& rIndexOffsets,
                  const RConnToConnIdxCalc& rConnToConnCalc,
                  const ConnIdxToCellIdxCalc& cellIdCalc,
                  vtkm::Id numberOfPoints,
                  vtkm::Id rConnSize)
  {
    using Algo = vtkm::cont::DeviceAdapterAlgorithm<Device>;

    auto connPortal = conn.PrepareForInput(Device());
    auto zeros = vtkm::cont::make_ArrayHandleConstant(vtkm::IdComponent{ 0 }, numberOfPoints);

    // Compute RConn offsets by atomically building a histogram and doing an
    // exclusive scan.
    //
    // Example:
    // (in)  Conn:  | 3  0  1  2  |  3  0  1  3  |  3  0  3  4  |  3  3  4  5  |
    // (out) RNumIndices:  3  2  1  3  2  1
    // (out) RIdxOffsets:  0  3  5  6  9  11
    { // allocate and zero the numIndices array:
      Algo::Copy(zeros, rNumIndices);
    }

    { // Build histogram:
      vtkm::exec::AtomicArray<vtkm::IdComponent, Device> atomicCounter{ rNumIndices };

      BuildHistogram histoGen{ atomicCounter, connPortal, rConnToConnCalc };

      Algo::Schedule(histoGen, rConnSize);
    }

    { // Compute offsets:
      auto rNumIndicesAsId = vtkm::cont::make_ArrayHandleCast<vtkm::Id>(rNumIndices);
      Algo::ScanExclusive(rNumIndicesAsId, rIndexOffsets);
    }

    { // Reset the numIndices array to 0's:
      Algo::Copy(zeros, rNumIndices);
    }

    // Fill the connectivity table:
    // 1) Lookup each point idx base offset.
    // 2) Use the atomic histogram to find the next available slot for this
    //    pt id in RConn.
    // 3) Compute the cell id from the connectivity index
    // 4) Update RConn[nextSlot] = cellId
    //
    // Example:
    // (in)    Conn:  | 3  0  1  2  |  3  0  1  3  |  3  0  3  4  |  3  3  4  5  |
    // (inout) RNumIndices:  0  0  0  0  0  0  (Initial)
    // (inout) RNumIndices:  3  2  1  3  2  1  (Final)
    // (in)    RIdxOffsets:  0  3  5  6  9  11
    // (out)   RConn: | 0  1  2  |  0  1  |  0  |  1  2  3  |  2  3  |  3  |
    {
      vtkm::exec::AtomicArray<vtkm::IdComponent, Device> atomicCounter{ rNumIndices };
      auto rOffsetPortal = rIndexOffsets.PrepareForInput(Device());
      auto rConnPortal = rConn.PrepareForOutput(rConnSize, Device());

      GenerateRConn rConnGen{ atomicCounter, connPortal,      rOffsetPortal,
                              rConnPortal,   rConnToConnCalc, cellIdCalc };

      Algo::Schedule(rConnGen, rConnSize);
    }
  }
};
}
}
} // end namespace vtkm::cont::internal

#endif // ReverseConnectivityBuilder_h