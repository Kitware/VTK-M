//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================
// Copyright (c) 2018, The Regents of the University of California, through
// Lawrence Berkeley National Laboratory (subject to receipt of any required approvals
// from the U.S. Dept. of Energy).  All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// (1) Redistributions of source code must retain the above copyright notice, this
//     list of conditions and the following disclaimer.
//
// (2) Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
// (3) Neither the name of the University of California, Lawrence Berkeley National
//     Laboratory, U.S. Dept. of Energy nor the names of its contributors may be
//     used to endorse or promote products derived from this software without
//     specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.
//
//=============================================================================
//
//  This code is an extension of the algorithm presented in the paper:
//  Parallel Peak Pruning for Scalable SMP Contour Tree Computation.
//  Hamish Carr, Gunther Weber, Christopher Sewell, and James Ahrens.
//  Proceedings of the IEEE Symposium on Large Data Analysis and Visualization
//  (LDAV), October 2016, Baltimore, Maryland.
//
//  The PPP2 algorithm and software were jointly developed by
//  Hamish Carr (University of Leeds), Gunther H. Weber (LBNL), and
//  Oliver Ruebel (LBNL)
//==============================================================================

#ifndef vtk_m_worklet_contourtree_augmented_active_graph_find_super_and_hyper_nodes_worklet_h
#define vtk_m_worklet_contourtree_augmented_active_graph_find_super_and_hyper_nodes_worklet_h

#include <vtkm/worklet/WorkletMapField.h>
#include <vtkm/worklet/contourtree_augmented/Types.h>

namespace vtkm
{
namespace worklet
{
namespace contourtree_augmented
{
namespace active_graph_inc
{

// Worklet for computing the sort indices from the sort order
class FindSuperAndHyperNodesWorklet : public vtkm::worklet::WorkletMapField
{
public:
  typedef void ControlSignature(FieldIn graphVertexIndex,          // (input) activeVertices
                                WholeArrayIn hyperarcs,            // (input) hyperarcs
                                WholeArrayIn newHypernodePosition, // (input) newHypernodePosition
                                WholeArrayIn newSupernodePosition, // (input) newSupernodePosition
                                WholeArrayOut hyperId,             // (output) hyperId
                                WholeArrayOut treeHypernodes, // (output) hypernodes of the tree
                                WholeArrayOut treeSupernodes  // (output) supernodes of the tree
                                );                            // (input/output) hyperarcs
  typedef void ExecutionSignature(_1, _2, _3, _4, _5, _6, _7);
  using InputDomain = _1;

  // Default Constructor
  VTKM_EXEC_CONT
  FindSuperAndHyperNodesWorklet() {}

  template <typename InFieldPortalType, typename OutFieldPortalType>
  VTKM_EXEC void operator()(const vtkm::Id& graphVertex,
                            const InFieldPortalType& hyperarcsPortal,
                            const InFieldPortalType& newHypernodePositionPortal,
                            const InFieldPortalType& newSupernodePositionPortal,
                            const OutFieldPortalType& hyperIdPortal,
                            const OutFieldPortalType& treeHypernodesPortal,
                            const OutFieldPortalType& treeSupernodesPortal) const
  {
    // retrieve the ID stored in the hyperarcs array
    vtkm::Id graphTarget = hyperarcsPortal.Get(graphVertex);

    // deal with hypernodes
    // must do this first, else supernode ID is incorrect
    if (IsHypernode(graphTarget))
    { // hypernode
      // set the IDs for later
      hyperIdPortal.Set(graphVertex, newHypernodePositionPortal.Get(graphVertex));
      treeHypernodesPortal.Set(newHypernodePositionPortal.Get(graphVertex), graphVertex);
    } // hypernode
    else
      // set the ID
      hyperIdPortal.Set(graphVertex, (vtkm::Id)NO_SUCH_ELEMENT);

    // supernode IDs
    if (IsSupernode(graphTarget))
    { // supernode
      // set the supernode indices up
      treeSupernodesPortal.Set(newSupernodePositionPortal.Get(graphVertex), graphVertex);
    } // supernode

    // In serial this worklet implements the following operation
    /*
      for (indexType graphVertex = 0; graphVertex < globalIndex.size(); graphVertex++)
        { // per graph vertex
          // retrieve the ID stored in the hyperarcs array
          indexType graphTarget = hyperarcs[graphVertex];

          // deal with hypernodes
          // must do this first, else supernode ID is incorrect
          if (IsHypernode(graphTarget))
            { // hypernode
              // set the IDs for later
              hyperID[graphVertex] = newHypernodePosition[graphVertex];
              tree.hypernodes[newHypernodePosition[graphVertex]] = graphVertex;
            } // hypernode
          else
            // set the ID
            hyperID[graphVertex] = NO_SUCH_ELEMENT;

          // supernode IDs
          if (IsSupernode(graphTarget))
            { // supernode
              // set the supernode indices up
              tree.supernodes[newSupernodePosition[graphVertex]] = graphVertex;
            } // supernode
        } // per graph vertex
      */
  }

}; // FindSuperAndHyperNodesWorklet

} // namespace active_graph_inc
} // namespace contourtree_augmented
} // namespace worklet
} // namespace vtkm

#endif
