//****************************************************************************
//
//  File:       LightShadow.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Core lighting and shadowing backend needs.
//              Not the actual rendering/execution (yet at least),
//              but the guts needed such as atlasing and tile clustering.
//
//****************************************************************************

#pragma once

#include "glvu.h"
#include "RenderScript.h"

namespace GLVU
{

    class IQueriableScene;

    /// Subdividing allocator for square blocks of a 2D domain. Doesn't actually care what purpose.
    class AtlasCellTable
    {
    public:
        /// Construct for a number of levels and a given total size (ie. 5 subdivisions of a 4096x4096 texture).
        AtlasCellTable(int levels, int dim);
        /// Construct for a fitment to a 64x64 minimum tile size.
        AtlasCellTable(int dim);

        /// Helper to hide need for levels knowledge.
        math::float4 GetArea(int size) { return GetCell(CalculateLevel(size)); }
        /// Try to get a texture-region for a given subdivision level.
        math::float4 GetCell(int level);
        /// Determines the subdivision levels based on an actual pixel-size (such as 64x64)
        int CalculateLevel(int forSize) const;
        /// Reset the state.
        void Clear();

        /// Convert into a viewport appropriate rect. As in glViewport
        math::uint4 ToViewport(math::float4) const;

        /// Sentinel value for comparisons.
        static const math::float4 InvalidCell;

        static void Divide(float4 input, float4* output);

    private:
        /// Inner handling of splitting a subdivision.
        void SplitCells(int level);
        /// Outer logical concerns for whether to split and from where in the tree to split if there is no one suitable.
        bool DivideCells(int level);

        /// Count of cells in each region.
        std::vector<int> cellCount_;
        /// There's never more than 4 cells needed for a given level at any time due to subdivision spiral
        std::vector< std::array<float4, 4> > regions_;

        /// LUT for mapping a pixel-size to a level of subdivision.
        std::vector<int> levelDims_;
        /// Intended dimensions of our texture.
        int dim_;
        /// Number of subdivision levels.
        int levels_;
    };

    /// Subdividing alloator for blocks of a 2D domain. Doesn't actually care what purpose.
    /// Unlike the AtlasCellTable this one supports retaining cells for caching purposes
    /// 
    class QuadTreeAllocator
    {
        struct Cell {
            Cell* children_ = nullptr;
            math::uint4 coords_;
            bool taken_ = false;

            ~Cell() {
                if (children_)
                    delete[] children_;
                children_ = nullptr;
            }

            void Clear() {
                if (children_)
                {
                    children_[0].Clear();
                    children_[1].Clear();
                    children_[2].Clear();
                    children_[3].Clear();
                }
                taken_ = false;
            }

            void Free() {
                if (children_)
                {
                    children_[0].Free();
                    children_[1].Free();
                    children_[2].Free();
                    children_[3].Free();
                }
                taken_ = false;
            }
            void Divide() {
                children_ = new Cell[4];
                
                const unsigned bX = coords_.x;
                const unsigned bY = coords_.y;
                const unsigned w = coords_.z / 2;
                const unsigned h = coords_.w / 2;
                children_[0].coords_ = math::uint4(bX, bY, w, h);
                children_[1].coords_ = math::uint4(bX + w, bY, w, h);
                children_[2].coords_ = math::uint4(bX, bY + h, w, h);
                children_[3].coords_ = math::uint4(bX + w, bY + h, w, h);
            }
            inline math::uint4 ToViewport() {
                return math::uint4::FromPosSize(coords_.x, coords_.y, coords_.z, coords_.w);
            }

            inline uint32_t Width() const { return coords_.z; }
            inline uint32_t Height() const { return coords_.w; }
            bool AnyTaken() const;
        };
        Cell* root_;

        math::uint4 GetCell(Cell*, uint32_t dim, uintptr_t datum = 0);
        void CollectRects(Cell*, std::vector<math::uint4>&);

        std::map<uintptr_t, std::pair<int, Cell*> > datumTable_;

    public:
        QuadTreeAllocator(uint32_t width, uint32_t height);
        ~QuadTreeAllocator();

        // as in glViewport
        inline math::uint4 ToViewport(math::float4 value) const {
            return uint4(value.x * width_, value.y * height_, value.Width() * width_, value.Height() * height_);
        }

        inline math::float4 ToFractional(math::uint4 value) const {
            return math::float4(value.x / (float)width_, value.y / (float)height_, value.z / (float)width_, value.w / (float)height_);
        }

        math::uint4 GetCell(uint32_t dim, uintptr_t datum = 0);
        void ProcessUpdate();
        void ReturnCell(uintptr_t datum);
        void CollectRects(std::vector<math::uint4>&);
        void Clear();

        uint32_t width_;
        uint32_t height_;
    };

    /// Basic utility wrapping the texture, FBO, and atlas management for a render-target atlas.
    struct RenderTargetAtlas
    {
        /// Construct.
        RenderTargetAtlas(GraphicsDevice*, TextureFormat format, uint32_t dim, bool withDepthBuffer = false);

        /// Attempt to allocate a space.
        math::float4 GetShadowRect(uint32_t dim);

        /// Convert a floating-point cell into integral coordinates suitable as a viewport rect.
        math::uint4 ToViewport(math::float4 v) const { return atlasTable_.ToViewport(v); }

        /// Resets the atlas table allocation.
        void Clear() { atlasTable_.Clear(); }

        QuadTreeAllocator atlasTable_;
        //AtlasCellTable atlasTable_;
        std::shared_ptr<Texture> shadowAtlas_;
        std::shared_ptr<FrameBuffer> shadowFBO_;
        uint32_t dim_;
    };

    /// Coordinates the management of lighting.
    struct LightTiler
    {
        LightTiler(GraphicsDevice* device, uint3 cells, uint32_t lightsPerCell);

        uint32_t BuildLightTables(Camera* camera, const std::vector< std::shared_ptr<Light> >& lights);
        uint32_t BuildLightTablesBounds(Camera* camera, const std::vector< std::shared_ptr<Light> >& lights);

        /// Stores the counts
        std::shared_ptr<Buffer> cellsUBO_;

        /// Stores the LightData structs.
        std::shared_ptr<Buffer> lightsUBO_;
        /// Stores the indexes for each cell that map a light to a LightData struct.
        std::shared_ptr<Buffer> lightIndexesUBO_;

        std::shared_ptr<Buffer> iblCubesUBO_;
        std::shared_ptr<Buffer> iblCubeIndexesUBO_;

        std::shared_ptr<Buffer> decalsUBO_;
        std::shared_ptr<Buffer> decalIndexesUBO_;

        std::shared_ptr<Texture> lightsTex_;

        GraphicsDevice* device_;
        math::uint3 tileDim_; // uze Z > 1 for clustered.
        uint32_t lightsPerCell_;
        uint32_t maxLights_;

        inline uint32_t toIndex(int x, int y, int z) const { return x + (y * tileDim_.x) + (z * tileDim_.x * tileDim_.y); }
        inline uint32_t CellCount() const { return tileDim_.x * tileDim_.y * tileDim_.z; }
        inline int toSliceZ(float z, float nearDist, float farDist) const {
            float logFrac = log(farDist / nearDist);
            return floorf((log(z) * (tileDim_.z / logFrac)) - ((tileDim_.z * log(nearDist)) / logFrac));
        }

        AABB ComputeFroxelBounds(Camera* camera, float x, float y, float z) const;
    };

    struct LightTiler_High {
        LightTiler_High(GraphicsDevice* device, uint3 cells, uint32_t lightsPerCell);
        void BuildLightTables(Camera* camera, const std::vector< std::shared_ptr<Light> >& lights);

        std::shared_ptr<Buffer> indexesSSBO_;
        std::shared_ptr<Buffer> lightsSSBO_;
        std::shared_ptr<Buffer> decalsSSBO_;
        std::shared_ptr<Buffer> cubesSSBO_;
    };
}