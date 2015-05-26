//
//   Copyright 2015 Pixar
//
//   Licensed under the Apache License, Version 2.0 (the "Apache License")
//   with the following modification; you may not use this file except in
//   compliance with the Apache License and the following modification to it:
//   Section 6. Trademarks. is deleted and replaced with:
//
//   6. Trademarks. This License does not grant permission to use the trade
//      names, trademarks, service marks, or product names of the Licensor
//      and its affiliates, except as required to comply with Section 4(c) of
//      the License and to reproduce the content of the NOTICE file.
//
//   You may obtain a copy of the Apache License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the Apache License with the above modification is
//   distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//   KIND, either express or implied. See the Apache License for the specific
//   language governing permissions and limitations under the Apache License.
//

#ifndef OPENSUBDIV_OPENSUBDIV3_OSD_CL_EVALUATOR_H
#define OPENSUBDIV_OPENSUBDIV3_OSD_CL_EVALUATOR_H

#include "../version.h"

#include "../osd/opencl.h"
#include "../osd/types.h"
#include "../osd/vertexDescriptor.h"

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

namespace Far {
    class StencilTable;
}

namespace Osd {

/// \brief OpenCL stencil table
///
/// This class is an OpenCL buffer representation of Far::StencilTable.
///
/// CLCompute consumes this table to apply stencils
///
///
class CLStencilTable {
public:
    template <typename DEVICE_CONTEXT>
    static CLStencilTable *Create(Far::StencilTable const *stencilTable,
                                  DEVICE_CONTEXT context) {
        return new CLStencilTable(stencilTable, context->GetContext());
    }

    CLStencilTable(Far::StencilTable const *stencilTable,
                   cl_context clContext);
    ~CLStencilTable();

    // interfaces needed for CLComputeKernel
    cl_mem GetSizesBuffer()     const { return _sizes; }
    cl_mem GetOffsetsBuffer()   const { return _offsets; }
    cl_mem GetIndicesBuffer()   const { return _indices; }
    cl_mem GetWeightsBuffer()   const { return _weights; }
    int GetNumStencils()        const { return _numStencils; }

private:
    cl_mem _sizes;
    cl_mem _offsets;
    cl_mem _indices;
    cl_mem _weights;
    int _numStencils;
};

// ---------------------------------------------------------------------------

class CLEvaluator {
public:
    typedef bool Instantiatable;
    /// Constructor.
    CLEvaluator(cl_context context, cl_command_queue queue);

    /// Desctructor.
    ~CLEvaluator();

    /// Generic creator template.
    template <typename DEVICE_CONTEXT>
    static CLEvaluator *Create(VertexBufferDescriptor const &srcDesc,
                               VertexBufferDescriptor const &dstDesc,
                               DEVICE_CONTEXT deviceContext) {
        return Create(srcDesc, dstDesc,
                      deviceContext->GetContext(),
                      deviceContext->GetCommandQueue());
    }

    static CLEvaluator * Create(VertexBufferDescriptor const &srcDesc,
                                VertexBufferDescriptor const &dstDesc,
                                cl_context clContext,
                                cl_command_queue clCommandQueue) {
        CLEvaluator *kernel = new CLEvaluator(clContext, clCommandQueue);
        if (kernel->Compile(srcDesc, dstDesc)) return kernel;
        delete kernel;
        return NULL;
    }

    /// ----------------------------------------------------------------------
    ///
    ///   Stencil evaluations with StencilTable
    ///
    /// ----------------------------------------------------------------------

    /// \brief Generic static compute function. This function has a same
    ///        signature as other device kernels have so that it can be called
    ///        transparently from OsdMesh template interface.
    ///
    /// @param srcBuffer      Input primvar buffer.
    ///                       must have BindCLBuffer() method returning a
    ///                       const float pointer for read
    ///
    /// @param srcDesc        vertex buffer descriptor for the input buffer
    ///
    /// @param dstBuffer      Output primvar buffer
    ///                       must have BindCLBuffer() method returning a
    ///                       float pointer for write
    ///
    /// @param dstDesc        vertex buffer descriptor for the output buffer
    ///
    /// @param stencilTable   stencil table to be applied. The table must have
    ///                       SSBO interfaces.
    ///
    /// @param instance       cached compiled instance. Clients are supposed to
    ///                       pre-compile an instance of this class and provide
    ///                       to this function. If it's null the kernel still
    ///                       compute by instantiating on-demand kernel although
    ///                       it may cause a performance problem.
    ///
    /// @param deviceContext  client providing context class which supports
    ///                         cL_context GetContext()
    ///                         cl_command_queue GetCommandQueue()
    ///                       methods.
    ///
    template <typename SRC_BUFFER, typename DST_BUFFER,
              typename STENCIL_TABLE, typename DEVICE_CONTEXT>
    static bool EvalStencils(
        SRC_BUFFER *srcBuffer, VertexBufferDescriptor const &srcDesc,
        DST_BUFFER *dstBuffer, VertexBufferDescriptor const &dstDesc,
        STENCIL_TABLE const *stencilTable,
        CLEvaluator const *instance,
        DEVICE_CONTEXT deviceContext) {

        if (instance) {
            return instance->EvalStencils(srcBuffer, srcDesc,
                                          dstBuffer, dstDesc,
                                          stencilTable);
        } else {
            // Create an instance on demand (slow)
            instance = Create(srcDesc, dstDesc, deviceContext);
            if (instance) {
                bool r = instance->EvalStencils(srcBuffer, srcDesc,
                                                dstBuffer, dstDesc,
                                                stencilTable);
                delete instance;
                return r;
            }
            return false;
        }
    }

    /// Generic compute function.
    /// Dispatch the CL compute kernel asynchronously.
    /// Returns false if the kernel hasn't been compiled yet.
    template <typename SRC_BUFFER, typename DST_BUFFER, typename STENCIL_TABLE>
    bool EvalStencils(
        SRC_BUFFER *srcBuffer, VertexBufferDescriptor const &srcDesc,
        DST_BUFFER *dstBuffer, VertexBufferDescriptor const &dstDesc,
        STENCIL_TABLE const *stencilTable) const {
        return EvalStencils(srcBuffer->BindCLBuffer(_clCommandQueue),
                            srcDesc,
                            dstBuffer->BindCLBuffer(_clCommandQueue),
                            dstDesc,
                            stencilTable->GetSizesBuffer(),
                            stencilTable->GetOffsetsBuffer(),
                            stencilTable->GetIndicesBuffer(),
                            stencilTable->GetWeightsBuffer(),
                            0,
                            stencilTable->GetNumStencils());
    }

    /// Dispatch the CL compute kernel asynchronously.
    /// returns false if the kernel hasn't been compiled yet.
    bool EvalStencils(cl_mem src, VertexBufferDescriptor const &srcDesc,
                      cl_mem dst, VertexBufferDescriptor const &dstDesc,
                      cl_mem sizes,
                      cl_mem offsets,
                      cl_mem indices,
                      cl_mem weights,
                      int start,
                      int end) const;

    /// ----------------------------------------------------------------------
    ///
    ///   Limit evaluations with PatchTable
    ///
    /// ----------------------------------------------------------------------
    ///
    /// \brief Generic limit eval function. This function has a same
    ///        signature as other device kernels have so that it can be called
    ///        in the same way.
    ///
    /// @param srcBuffer      Input primvar buffer.
    ///                       must have BindCLBuffer() method returning a CL
    ///                       buffer object of source data
    ///
    /// @param srcDesc        vertex buffer descriptor for the input buffer
    ///
    /// @param dstBuffer      Output primvar buffer
    ///                       must have BindCLBuffer() method returning a CL
    ///                       buffer object of destination data
    ///
    /// @param dstDesc        vertex buffer descriptor for the output buffer
    ///
    /// @param numPatchCoords number of patchCoords.
    ///
    /// @param patchCoords    array of locations to be evaluated.
    ///                       must have BindCLBuffer() method returning an
    ///                       array of PatchCoord struct.
    ///
    /// @param patchTable     CLPatchTable or equivalent
    ///
    /// @param instance       cached compiled instance. Clients are supposed to
    ///                       pre-compile an instance of this class and provide
    ///                       to this function. If it's null the kernel still
    ///                       compute by instantiating on-demand kernel although
    ///                       it may cause a performance problem.
    ///
    /// @param deviceContext  client providing context class which supports
    ///                         cL_context GetContext()
    ///                         cl_command_queue GetCommandQueue()
    ///                       methods.
    ///
    template <typename SRC_BUFFER, typename DST_BUFFER,
              typename PATCHCOORD_BUFFER, typename PATCH_TABLE,
              typename DEVICE_CONTEXT>
    static bool EvalPatches(
        SRC_BUFFER *srcBuffer, VertexBufferDescriptor const &srcDesc,
        DST_BUFFER *dstBuffer, VertexBufferDescriptor const &dstDesc,
        int numPatchCoords,
        PATCHCOORD_BUFFER *patchCoords,
        PATCH_TABLE *patchTable,
        CLEvaluator const *instance,
        DEVICE_CONTEXT deviceContext) {

        if (instance) {
            return instance->EvalPatches(srcBuffer, srcDesc,
                                         dstBuffer, dstDesc,
                                         numPatchCoords, patchCoords,
                                         patchTable);
        } else {
            // Create an instance on demand (slow)
            (void)deviceContext;  // unused
            instance = Create(srcDesc, dstDesc, deviceContext);
            if (instance) {
                bool r = instance->EvalPatches(srcBuffer, srcDesc,
                                               dstBuffer, dstDesc,
                                               numPatchCoords, patchCoords,
                                               patchTable);
                delete instance;
                return r;
            }
            return false;
        }
    }

    /// \brief Generic limit eval function. This function has a same
    ///        signature as other device kernels have so that it can be called
    ///        in the same way.
    ///
    /// @param srcBuffer      Input primvar buffer.
    ///                       must have BindCLBuffer() method returning a CL
    ///                       buffer object of source data
    ///
    /// @param srcDesc        vertex buffer descriptor for the input buffer
    ///
    /// @param dstBuffer      Output primvar buffer
    ///                       must have BindCLBuffer() method returning a CL
    ///                       buffer object of destination data
    ///
    /// @param dstDesc        vertex buffer descriptor for the output buffer
    ///
    /// @param duBuffer
    ///
    /// @param duDesc
    ///
    /// @param dvBuffer
    ///
    /// @param dvDesc
    ///
    /// @param numPatchCoords number of patchCoords.
    ///
    /// @param patchCoords    array of locations to be evaluated.
    ///                       must have BindCLBuffer() method returning an
    ///                       array of PatchCoord struct
    ///
    /// @param patchTable     CLPatchTable or equivalent
    ///
    /// @param instance       cached compiled instance. Clients are supposed to
    ///                       pre-compile an instance of this class and provide
    ///                       to this function. If it's null the kernel still
    ///                       compute by instantiating on-demand kernel although
    ///                       it may cause a performance problem.
    ///
    /// @param deviceContext  client providing context class which supports
    ///                         cL_context GetContext()
    ///                         cl_command_queue GetCommandQueue()
    ///                       methods.
    ///
    template <typename SRC_BUFFER, typename DST_BUFFER,
              typename PATCHCOORD_BUFFER, typename PATCH_TABLE,
              typename DEVICE_CONTEXT>
    static bool EvalPatches(
        SRC_BUFFER *srcBuffer, VertexBufferDescriptor const &srcDesc,
        DST_BUFFER *dstBuffer, VertexBufferDescriptor const &dstDesc,
        DST_BUFFER *duBuffer,  VertexBufferDescriptor const &duDesc,
        DST_BUFFER *dvBuffer,  VertexBufferDescriptor const &dvDesc,
        int numPatchCoords,
        PATCHCOORD_BUFFER *patchCoords,
        PATCH_TABLE *patchTable,
        CLEvaluator const *instance,
        DEVICE_CONTEXT deviceContext) {

        if (instance) {
            return instance->EvalPatches(srcBuffer, srcDesc,
                                         dstBuffer, dstDesc,
                                         duBuffer, duDesc,
                                         dvBuffer, dvDesc,
                                         numPatchCoords, patchCoords,
                                         patchTable);
        } else {
            // Create an instance on demand (slow)
            (void)deviceContext;  // unused
            instance = Create(srcDesc, dstDesc, deviceContext);
            if (instance) {
                bool r = instance->EvalPatches(srcBuffer, srcDesc,
                                               dstBuffer, dstDesc,
                                               duBuffer, duDesc,
                                               dvBuffer, dvDesc,
                                               numPatchCoords, patchCoords,
                                               patchTable);
                delete instance;
                return r;
            }
            return false;
        }
    }

    /// \brief Generic limit eval function. This function has a same
    ///        signature as other device kernels have so that it can be called
    ///        in the same way.
    ///
    /// @param srcBuffer      Input primvar buffer.
    ///                       must have BindCLBuffer() method returning a CL
    ///                       buffer object of source data
    ///
    /// @param srcDesc        vertex buffer descriptor for the input buffer
    ///
    /// @param dstBuffer      Output primvar buffer
    ///                       must have BindCLBuffer() method returning a CL
    ///                       buffer object of destination data
    ///
    /// @param dstDesc        vertex buffer descriptor for the output buffer
    ///
    /// @param numPatchCoords number of patchCoords.
    ///
    /// @param patchCoords    array of locations to be evaluated.
    ///                       must have BindCLBuffer() method returning an
    ///                       array of PatchCoord struct.
    ///
    /// @param patchTable     CLPatchTable or equivalent
    ///
    template <typename SRC_BUFFER, typename DST_BUFFER,
              typename PATCHCOORD_BUFFER, typename PATCH_TABLE>
    bool EvalPatches(
        SRC_BUFFER *srcBuffer, VertexBufferDescriptor const &srcDesc,
        DST_BUFFER *dstBuffer, VertexBufferDescriptor const &dstDesc,
        int numPatchCoords,
        PATCHCOORD_BUFFER *patchCoords,
        PATCH_TABLE *patchTable) const {

        return EvalPatches(srcBuffer->BindCLBuffer(_clCommandQueue), srcDesc,
                           dstBuffer->BindCLBuffer(_clCommandQueue), dstDesc,
                           0, VertexBufferDescriptor(),
                           0, VertexBufferDescriptor(),
                           numPatchCoords,
                           patchCoords->BindCLBuffer(_clCommandQueue),
                           patchTable->GetPatchArrayBuffer(),
                           patchTable->GetPatchIndexBuffer(),
                           patchTable->GetPatchParamBuffer());
    }

    /// \brief Generic limit eval function with derivatives. This function has
    ///        a same signature as other device kernels have so that it can be
    ///        called in the same way.
    ///
    /// @param srcBuffer        Input primvar buffer.
    ///                         must have BindCLBuffer() method returning a CL
    ///                         buffer object of source data
    ///
    /// @param srcDesc          vertex buffer descriptor for the input buffer
    ///
    /// @param dstBuffer        Output primvar buffer
    ///                         must have BindCLBuffer() method returning a CL
    ///                         buffer object of destination data
    ///
    /// @param dstDesc          vertex buffer descriptor for the output buffer
    ///
    /// @param duBuffer         Output U-derivatives buffer
    ///                         must have BindCLBuffer() method returning a CL
    ///                         buffer object of destination data of Du
    ///
    /// @param duDesc           vertex buffer descriptor for the duBuffer
    ///
    /// @param dvBuffer         Output V-derivatives buffer
    ///                         must have BindCLBuffer() method returning a CL
    ///                         buffer object of destination data of Dv
    ///
    /// @param dvDesc           vertex buffer descriptor for the dvBuffer
    ///
    /// @param numPatchCoords   number of patchCoords.
    ///
    /// @param patchCoords      array of locations to be evaluated.
    ///
    /// @param patchTable       CLPatchTable or equivalent
    ///
    template <typename SRC_BUFFER, typename DST_BUFFER,
              typename PATCHCOORD_BUFFER, typename PATCH_TABLE>
    bool EvalPatches(
        SRC_BUFFER *srcBuffer, VertexBufferDescriptor const &srcDesc,
        DST_BUFFER *dstBuffer, VertexBufferDescriptor const &dstDesc,
        DST_BUFFER *duBuffer,  VertexBufferDescriptor const &duDesc,
        DST_BUFFER *dvBuffer,  VertexBufferDescriptor const &dvDesc,
        int numPatchCoords,
        PATCHCOORD_BUFFER *patchCoords,
        PATCH_TABLE *patchTable) const {

        return EvalPatches(srcBuffer->BindCLBuffer(_clCommandQueue), srcDesc,
                           dstBuffer->BindCLBuffer(_clCommandQueue), dstDesc,
                           duBuffer->BindCLBuffer(_clCommandQueue),  duDesc,
                           dvBuffer->BindCLBuffer(_clCommandQueue),  dvDesc,
                           numPatchCoords,
                           patchCoords->BindCLBuffer(_clCommandQueue),
                           patchTable->GetPatchArrayBuffer(),
                           patchTable->GetPatchIndexBuffer(),
                           patchTable->GetPatchParamBuffer());
    }

    bool EvalPatches(cl_mem src, VertexBufferDescriptor const &srcDesc,
                     cl_mem dst, VertexBufferDescriptor const &dstDesc,
                     cl_mem du,  VertexBufferDescriptor const &duDesc,
                     cl_mem dv,  VertexBufferDescriptor const &dvDesc,
                     int numPatchCoords,
                     cl_mem patchCoordsBuffer,
                     cl_mem patchArrayBuffer,
                     cl_mem patchIndexBuffer,
                     cl_mem patchParamsBuffer) const;

    /// ----------------------------------------------------------------------
    ///
    ///   Other methods
    ///
    /// ----------------------------------------------------------------------

    /// Configure OpenCL kernel.
    /// Returns false if it fails to compile the kernel.
    bool Compile(VertexBufferDescriptor const &srcDesc,
                 VertexBufferDescriptor const &dstDesc);

    /// Wait the OpenCL kernels finish.
    template <typename DEVICE_CONTEXT>
    static void Synchronize(DEVICE_CONTEXT deviceContext) {
        Synchronize(deviceContext->GetCommandQueue());
    }

    static void Synchronize(cl_command_queue queue);

private:
    cl_context _clContext;
    cl_command_queue _clCommandQueue;
    cl_program _program;
    cl_kernel _stencilKernel;
    cl_kernel _patchKernel;
};


}  // end namespace Osd

}  // end namespace OPENSUBDIV_VERSION
using namespace OPENSUBDIV_VERSION;

}  // end namespace OpenSubdiv


#endif  // OPENSUBDIV_OPENSUBDIV3_OSD_CL_EVALUATOR_H
