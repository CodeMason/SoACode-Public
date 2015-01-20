#include "stdafx.h"
#include "SphericalTerrainPatch.h"

#include <Vorb/graphics/GpuMemory.h>
#include <Vorb/TextureRecycler.hpp>
#include <Vorb/utils.h>

#include "Camera.h"
#include "Chunk.h"
#include "RPC.h"
#include "RenderUtils.h"
#include "SphericalTerrainComponent.h"
#include "SphericalTerrainGenerator.h"
#include "VoxelPlanetMapper.h"

const f32v3 NormalMults[6] = {
    f32v3(1.0f, 1.0f, -1.0f), //TOP
    f32v3(1.0f, 1.0f, -1.0f), //LEFT
    f32v3(1.0f, 1.0f, 1.0f), //RIGHT
    f32v3(1.0f, 1.0f, 1.0f), //FRONT
    f32v3(1.0f, 1.0f, -1.0f), //BACK
    f32v3(1.0f, 1.0f, 1.0f) //BOTTOM
};

SphericalTerrainMesh::~SphericalTerrainMesh() {
    if (m_vbo) {
        vg::GpuMemory::freeBuffer(m_vbo);
    }
    if (m_wvbo) {
        vg::GpuMemory::freeBuffer(m_wvbo);
    }
    if (m_wibo) {
        vg::GpuMemory::freeBuffer(m_wibo);
    }
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
    }
    if (m_normalMap) {
        glDeleteTextures(1, &m_normalMap);
    }
}

void SphericalTerrainMesh::recycleNormalMap(vg::TextureRecycler* recycler) {
    if (m_normalMap) {
        recycler->recycle(m_normalMap);
        m_normalMap = 0;
    }
}

void SphericalTerrainMesh::draw(const f64v3& cameraPos, const Camera* camera,
                                const f32m4& rot, vg::GLProgram* program) {
    // Set up matrix
    f32m4 W(1.0);
    setMatrixTranslation(W, -cameraPos);

    f32m4 WVP = camera->getViewProjectionMatrix() * W * rot;
    W *= rot;

    // TODO(Ben): GOT A CRASH HERE!
    if ((int)m_cubeFace < 0 || (int)m_cubeFace > 6) {
        pError("m_cubeFace is invalid in draw!");
    }

    glUniform3fv(program->getUniform("unNormMult"), 1, &NormalMults[(int)m_cubeFace][0]);
    glUniformMatrix4fv(program->getUniform("unWVP"), 1, GL_FALSE, &WVP[0][0]);
    glUniformMatrix4fv(program->getUniform("unW"), 1, GL_FALSE, &W[0][0]);

    // TODO: Using a VAO makes it not work??
   // glBindVertexArray(m_vao);

    glBindTexture(GL_TEXTURE_2D, m_normalMap);

    vg::GpuMemory::bindBuffer(m_vbo, vg::BufferTarget::ARRAY_BUFFER);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(TerrainVertex),
                          offsetptr(TerrainVertex, position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          sizeof(TerrainVertex),
                          offsetptr(TerrainVertex, tangent));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                          sizeof(TerrainVertex),
                          offsetptr(TerrainVertex, texCoords));
    glVertexAttribPointer(3, 3, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(TerrainVertex),
                          offsetptr(TerrainVertex, color));
    glVertexAttribPointer(4, 2, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(TerrainVertex),
                          offsetptr(TerrainVertex, normTexCoords));
    glVertexAttribPointer(5, 2, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(TerrainVertex),
                          offsetptr(TerrainVertex, temperature));

    vg::GpuMemory::bindBuffer(m_ibo, vg::BufferTarget::ELEMENT_ARRAY_BUFFER);
    glDrawElements(GL_TRIANGLES, SphericalTerrainPatch::INDICES_PER_PATCH, GL_UNSIGNED_SHORT, 0);
 //   glBindVertexArray(0);
}

void SphericalTerrainMesh::drawWater(const f64v3& cameraPos, const Camera* camera,
                                     const f32m4& rot, vg::GLProgram* program) {
    // Set up matrix
    f32m4 W(1.0);
    setMatrixTranslation(W, -cameraPos);
    W *= rot;
    f32m4 WVP = camera->getViewProjectionMatrix() * W;

    // TODO(Ben): GOT A CRASH HERE!
    if ((int)m_cubeFace < 0 || (int)m_cubeFace > 6) {
        pError("m_cubeFace is invalid in drawWater!");
    }

    glUniform3fv(program->getUniform("unNormMult"), 1, &NormalMults[(int)m_cubeFace][0]);
    glUniformMatrix4fv(program->getUniform("unWVP"), 1, GL_FALSE, &WVP[0][0]);
    glUniformMatrix4fv(program->getUniform("unW"), 1, GL_FALSE, &W[0][0]);

    // TODO: Using a VAO makes it not work??
    // glBindVertexArray(m_vao);

    vg::GpuMemory::bindBuffer(m_wvbo, vg::BufferTarget::ARRAY_BUFFER);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(WaterVertex),
                          offsetptr(WaterVertex, position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          sizeof(WaterVertex),
                          offsetptr(WaterVertex, tangent));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(WaterVertex),
                          offsetptr(WaterVertex, color));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_TRUE,
                          sizeof(WaterVertex),
                          offsetptr(WaterVertex, texCoords));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE,
                          sizeof(WaterVertex),
                          offsetptr(WaterVertex, depth));


    vg::GpuMemory::bindBuffer(m_wibo, vg::BufferTarget::ELEMENT_ARRAY_BUFFER);
    glDrawElements(GL_TRIANGLES, m_waterIndexCount, GL_UNSIGNED_SHORT, 0);

    //   glBindVertexArray(0);
}


SphericalTerrainPatch::~SphericalTerrainPatch() {
    destroy();
}

void SphericalTerrainPatch::init(const f64v2& gridPosition,
                                 CubeFace cubeFace,
                                 int lod,
                                 const SphericalTerrainData* sphericalTerrainData,
                                 f64 width,
                                 TerrainRpcDispatcher* dispatcher) {
    m_gridPosition = gridPosition;
    m_cubeFace = cubeFace;
    m_lod = lod;
    m_sphericalTerrainData = sphericalTerrainData;
    m_width = width;
    m_dispatcher = dispatcher;

    // Approximate the world position for now //TODO(Ben): Better
    f64v2 centerGridPos = gridPosition + f64v2(width / 2.0);

    const i32v3& coordMapping = CubeCoordinateMappings[(int)m_cubeFace];
    const f32v3& coordMults = CubeCoordinateMults[(int)m_cubeFace];

    m_worldPosition[coordMapping.x] = centerGridPos.x;
    m_worldPosition[coordMapping.y] = sphericalTerrainData->getRadius() * coordMults.y;
    m_worldPosition[coordMapping.z] = centerGridPos.y;

    m_worldPosition = glm::normalize(m_worldPosition);
}

void SphericalTerrainPatch::update(const f64v3& cameraPos) {
    const float DIST_MIN = 3.0f;
    const float DIST_MAX = 3.1f;

#define MIN_SIZE 0.016f

    // Calculate distance from camera
    if (hasMesh()) {
        m_worldPosition = m_mesh->m_worldPosition;
        m_distance = glm::length(m_worldPosition - cameraPos);
    } else {
        m_distance = glm::length(m_worldPosition - cameraPos);
    }
    
    if (m_children) {
   
        if (m_distance > m_width * DIST_MAX) {
            if (!m_mesh) {
                requestMesh();
            }
            if (hasMesh()) {
                // Out of range, kill children
                delete[] m_children;
                m_children = nullptr;
            }
        } else if (m_mesh) {
            // In range, but we need to remove our mesh.
            // Check to see if all children are renderable
            bool deleteMesh = true;
            for (int i = 0; i < 4; i++) {
                if (!m_children[i].isRenderable()) {
                    deleteMesh = false;
                    break;
                }
            }
            
            if (deleteMesh) {
                // Children are renderable, free mesh.
                // Render thread will deallocate.
                m_mesh->m_shouldDelete = true;
                m_mesh = nullptr;
            }
        }
    } else if (m_lod < MAX_LOD && m_distance < m_width * DIST_MIN && m_width > MIN_SIZE) {
        m_children = new SphericalTerrainPatch[4];
        // Segment into 4 children
        for (int z = 0; z < 2; z++) {
            for (int x = 0; x < 2; x++) {
                m_children[(z << 1) + x].init(m_gridPosition + f64v2((m_width / 2.0) * x, (m_width / 2.0) * z),
                                       m_cubeFace, m_lod + 1, m_sphericalTerrainData, m_width / 2.0,
                                       m_dispatcher);
            }
        }
    } else if (!m_mesh) {
        requestMesh();
    }
    
    // Recursively update children if they exist
    if (m_children) {
        for (int i = 0; i < 4; i++) {
            m_children[i].update(cameraPos);
        }
    }
}

void SphericalTerrainPatch::destroy() {
    if (m_mesh) {
        m_mesh->m_shouldDelete = true;
        m_mesh = nullptr;
    }
    delete[] m_children;
    m_children = nullptr;
}

bool SphericalTerrainPatch::isRenderable() const {
    if (hasMesh()) return true;
    if (m_children) {
        for (int i = 0; i < 4; i++) {
            if (!m_children[i].isRenderable()) return false;
        }
        return true;
    }
    return false;
}

void SphericalTerrainPatch::requestMesh() {
    // Try to generate a mesh
    const f32v3& mults = CubeCoordinateMults[(int)m_cubeFace];
    const i32v3& mappings = CubeCoordinateMappings[(int)m_cubeFace];

    f32v3 startPos(m_gridPosition.x * mults.x,
                   m_sphericalTerrainData->getRadius() * mults.y,
                   m_gridPosition.y* mults.z);
    m_mesh = m_dispatcher->dispatchTerrainGen(startPos,
                                              mappings,
                                              m_width,
                                              m_lod,
                                              m_cubeFace);
}