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
#ifndef vtk_m_worklet_zfp_encode1_h
#define vtk_m_worklet_zfp_encode1_h

#include <vtkm/Types.h>
#include <vtkm/internal/ExportMacros.h>

#include <vtkm/worklet/WorkletMapField.h>
#include <vtkm/worklet/zfp/ZFPBlockWriter.h>
#include <vtkm/worklet/zfp/ZFPEncode.h>
#include <vtkm/worklet/zfp/ZFPFunctions.h>
#include <vtkm/worklet/zfp/ZFPStructs.h>
#include <vtkm/worklet/zfp/ZFPTypeInfo.h>



namespace vtkm
{
namespace worklet
{
namespace zfp
{

template <typename Scalar, typename PortalType>
VTKM_EXEC inline void GatherPartial1(Scalar* q,
                                     const PortalType& scalars,
                                     vtkm::Id offset,
                                     int nx,
                                     int sx)
{
  vtkm::Id x;
  for (x = 0; x < nx; x++, offset += sx)
    q[x] = scalars.Get(offset);
  PadBlock(q, vtkm::UInt32(nx), 1);
}

template <typename Scalar, typename PortalType>
VTKM_EXEC inline void Gather1(Scalar* fblock, const PortalType& scalars, vtkm::Id offset, int sx)
{
  vtkm::Id counter = 0;

  for (vtkm::Id x = 0; x < 4; x++, offset += sx)
  {
    fblock[counter] = scalars.Get(offset);
    counter++;
  }
}

struct Encode1 : public vtkm::worklet::WorkletMapField
{
protected:
  vtkm::Id Dims;        // field dims
  vtkm::Id PaddedDims;  // dims padded to a multiple of zfp block size
  vtkm::Id ZFPDims;     // zfp block dims
  vtkm::UInt32 MaxBits; // bits per zfp block

public:
  Encode1(const vtkm::Id dims, const vtkm::Id paddedDims, const vtkm::UInt32 maxbits)
    : Dims(dims)
    , PaddedDims(paddedDims)
    , MaxBits(maxbits)
  {
    ZFPDims = PaddedDims / 4;
  }
  using ControlSignature = void(FieldIn<>, WholeArrayIn<>, AtomicArrayInOut<> bitstream);
  using ExecutionSignature = void(_1, _2, _3);

  template <class InputScalarPortal, typename BitstreamPortal>
  VTKM_EXEC void operator()(const vtkm::Id blockIdx,
                            const InputScalarPortal& scalars,
                            BitstreamPortal& stream) const
  {
    using Scalar = typename InputScalarPortal::ValueType;

    //    typedef unsigned long long int ull;
    //    typedef long long int ll;
    //    const ull blockId = blockIdx.x +
    //                        blockIdx.y * gridDim.x +
    //                        gridDim.x * gridDim.y * blockIdx.z;

    //    // each thread gets a block so the block index is
    //    // the global thread index
    //    const uint block_idx = blockId * blockDim.x + threadIdx.x;

    //    if(block_idx >= tot_blocks)
    //    {
    //      // we can't launch the exact number of blocks
    //      // so just exit if this isn't real
    //      return;
    //    }

    //    uint2 block_dims;
    //    block_dims.x = padded_dims.x >> 2;
    //    block_dims.y = padded_dims.y >> 2;

    //    // logical pos in 3d array
    //    uint2 block;
    //    block.x = (block_idx % block_dims.x) * 4;
    //    block.y = ((block_idx/ block_dims.x) % block_dims.y) * 4;
    //    const ll offset = (ll)block.x * stride.x + (ll)block.y * stride.y;

    vtkm::Id zfpBlock;
    zfpBlock = blockIdx % ZFPDims;
    vtkm::Id logicalStart = zfpBlock * vtkm::Id(4);

    constexpr vtkm::Int32 BlockSize = 4;
    Scalar fblock[BlockSize];

    //    bool partial = false;
    //    if(block.x + 4 > dims.x) partial = true;
    //    if(block.y + 4 > dims.y) partial = true;

    bool partial = false;
    if (logicalStart + 4 > Dims)
      partial = true;

    if (partial)
    {
      const vtkm::Int32 nx =
        logicalStart + 4 > Dims ? vtkm::Int32(Dims - logicalStart) : vtkm::Int32(4);
      GatherPartial1(fblock, scalars, logicalStart, nx, 1);
    }
    else
    {
      Gather1(fblock, scalars, logicalStart, 1);
    }


    //zfp_encode_block<Scalar, ZFP_2D_BLOCK_SIZE>(fblock, maxbits, block_idx, stream);
    zfp::ZFPBlockEncoder<BlockSize, Scalar, BitstreamPortal> encoder;
    encoder.encode(fblock, static_cast<vtkm::Int32>(MaxBits), vtkm::UInt32(blockIdx), stream);
  }
};
}
}
}
#endif