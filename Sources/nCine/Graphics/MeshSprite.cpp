#include "MeshSprite.h"
#include "RenderCommand.h"
#include "RenderResources.h"

#include <cstring> // for memcpy()

namespace nCine
{
	MeshSprite::MeshSprite()
		: MeshSprite(nullptr, nullptr, 0.0f, 0.0f)
	{
	}

	MeshSprite::MeshSprite(SceneNode* parent, Texture* texture)
		: MeshSprite(parent, texture, 0.0f, 0.0f)
	{
	}

	MeshSprite::MeshSprite(Texture* texture)
		: MeshSprite(nullptr, texture, 0.0f, 0.0f)
	{
	}

	MeshSprite::MeshSprite(SceneNode* parent, Texture* texture, float xx, float yy)
		: BaseSprite(parent, texture, xx, yy), vertices_(16), vertexDataPointer_(nullptr), bytesPerVertex_(0),
			numVertices_(0), indices_(16), indexDataPointer_(nullptr), numIndices_(0)
	{
		init();
	}

	MeshSprite::MeshSprite(SceneNode* parent, Texture* texture, Vector2f position)
		: MeshSprite(parent, texture, position.X, position.Y)
	{
	}

	MeshSprite::MeshSprite(Texture* texture, float xx, float yy)
		: MeshSprite(nullptr, texture, xx, yy)
	{
	}

	MeshSprite::MeshSprite(Texture* texture, Vector2f position)
		: MeshSprite(nullptr, texture, position.X, position.Y)
	{
	}

	/*! \note If used directly, it requires a custom shader that understands the specified data format */
	void MeshSprite::copyVertices(std::uint32_t numVertices, std::uint32_t bytesPerVertex, const void* vertexData)
	{
		std::uint32_t floatsPerVertex = (bytesPerVertex / sizeof(float));
		vertices_.resize(numVertices * floatsPerVertex);
		memcpy(vertices_.data(), vertexData, numVertices * bytesPerVertex);
		bytesPerVertex_ = bytesPerVertex;

		vertexDataPointer_ = vertices_.data();
		numVertices_ = numVertices;
		renderCommand_.GetGeometry().SetVertexCount(numVertices);
		renderCommand_.GetGeometry().SetElementsPerVertex(floatsPerVertex);
		renderCommand_.GetGeometry().SetHostVertexPointer(vertexDataPointer_);
	}

	void MeshSprite::copyVertices(std::uint32_t numVertices, const Vertex* vertices)
	{
		DEATH_ASSERT(texture_ != nullptr);
		copyVertices(numVertices, sizeof(Vertex), reinterpret_cast<const float*>(vertices));
	}

	void MeshSprite::copyVertices(std::uint32_t numVertices, const VertexNoTexture* vertices)
	{
		DEATH_ASSERT(texture_ == nullptr);
		copyVertices(numVertices, sizeof(VertexNoTexture), reinterpret_cast<const float*>(vertices));
	}

	void MeshSprite::copyVertices(const MeshSprite& meshSprite)
	{
		copyVertices(meshSprite.numVertices_, meshSprite.bytesPerVertex_, meshSprite.vertexDataPointer_);
		width_ = meshSprite.width_;
		height_ = meshSprite.height_;
		texRect_ = meshSprite.texRect_;

		dirtyBits_.set(DirtyBitPositions::SizeBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
		dirtyBits_.set(DirtyBitPositions::TextureBit);
	}

	/*! \note If used directly, it requires a custom shader that understands the specified data format. */
	void MeshSprite::setVertices(std::uint32_t numVertices, std::uint32_t bytesPerVertex, const void* vertexData)
	{
		std::uint32_t floatsPerVertex = (bytesPerVertex / sizeof(float));
		vertices_.clear();
		bytesPerVertex_ = bytesPerVertex;

		vertexDataPointer_ = reinterpret_cast<const float*>(vertexData);
		numVertices_ = numVertices;
		renderCommand_.GetGeometry().SetVertexCount(numVertices);
		renderCommand_.GetGeometry().SetElementsPerVertex(floatsPerVertex);
		renderCommand_.GetGeometry().SetHostVertexPointer(vertexDataPointer_);
	}

	void MeshSprite::setVertices(std::uint32_t numVertices, const Vertex* vertices)
	{
		DEATH_ASSERT(texture_ != nullptr);
		copyVertices(numVertices, sizeof(Vertex), reinterpret_cast<const void*>(vertices));
	}

	void MeshSprite::setVertices(std::uint32_t numVertices, const VertexNoTexture* vertices)
	{
		DEATH_ASSERT(texture_ == nullptr);
		copyVertices(numVertices, sizeof(VertexNoTexture), reinterpret_cast<const void*>(vertices));
	}

	void MeshSprite::setVertices(const MeshSprite& meshSprite)
	{
		setVertices(meshSprite.numVertices_, meshSprite.bytesPerVertex_, meshSprite.vertexDataPointer_);
		width_ = meshSprite.width_;
		height_ = meshSprite.height_;

		dirtyBits_.set(DirtyBitPositions::SizeBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	float* MeshSprite::emplaceVertices(std::uint32_t numElements, std::uint32_t bytesPerVertex)
	{
		if (numElements == 0 || bytesPerVertex == 0) {
			return nullptr;
		}

		const std::uint32_t floatsPerVertex = bytesPerVertex / sizeof(float);
		const std::uint32_t numVertices = (numElements / floatsPerVertex);
		vertices_.clear();
		vertices_.resize(numElements);
		bytesPerVertex_ = bytesPerVertex;

		vertexDataPointer_ = vertices_.data();
		numVertices_ = numVertices;
		renderCommand_.GetGeometry().SetVertexCount(numVertices);
		renderCommand_.GetGeometry().SetElementsPerVertex(floatsPerVertex);
		renderCommand_.GetGeometry().SetHostVertexPointer(vertexDataPointer_);

		return vertices_.data();
	}

	float* MeshSprite::emplaceVertices(std::uint32_t numElements)
	{
		const std::uint32_t bytesPerVertex = (texture_ != nullptr ? sizeof(Vertex) : sizeof(VertexNoTexture));
		return emplaceVertices(numElements, bytesPerVertex);
	}

	void MeshSprite::createVerticesFromTexels(std::uint32_t numVertices, const Vector2f* points, TextureCutMode cutMode)
	{
		FATAL_ASSERT(numVertices >= 3);

		const std::uint32_t numFloats = (texture_ != nullptr ? VertexFloats : VertexNoTextureFloats);
		vertices_.resize(numVertices * numFloats);
		bytesPerVertex_ = (texture_ != nullptr ? sizeof(Vertex) : sizeof(VertexNoTexture));
		Vector2f min(0.0f, 0.0f);

		if (cutMode == TextureCutMode::CROP) {
			min = points[0];
			Vector2f max(min);
			for (std::uint32_t i = 1; i < numVertices; i++) {
				if (points[i].X > max.X) {
					max.X = points[i].X;
				} else if (points[i].X < min.X) {
					min.X = points[i].X;
				}
				if (points[i].Y > max.Y) {
					max.Y = points[i].Y;
				} else if (points[i].Y < min.Y) {
					min.Y = points[i].Y;
				}
			}

			width_ = max.X - min.X;
			height_ = max.Y - min.Y;
		} else if (texRect_.W > 0 && texRect_.H > 0) {
			width_ = float(texRect_.W);
			height_ = float(texRect_.H);
		}

		const float halfWidth = width_ * 0.5f;
		const float halfHeight = height_ * 0.5f;

		for (std::uint32_t i = 0; i < numVertices; i++) {
			if (texture_ != nullptr) {
				Vertex& v = reinterpret_cast<Vertex&>(vertices_[i * VertexFloats]);
				v.x = (points[i].X - min.X - halfWidth) / width_; // from -0.5 to 0.5
				v.y = (points[i].Y - min.Y - halfHeight) / height_; // from -0.5 to 0.5
				v.u = points[i].X / (texRect_.W - texRect_.X);
				v.v = (texRect_.H - points[i].Y) / (texRect_.H - texRect_.Y); // flipped
			} else {
				VertexNoTexture& v = reinterpret_cast<VertexNoTexture&>(vertices_[i * VertexNoTextureFloats]);
				v.x = (points[i].X - min.X - halfWidth) / width_; // from -0.5 to 0.5
				v.y = (points[i].Y - min.Y - halfHeight) / height_; // from -0.5 to 0.5
			}
		}

		vertexDataPointer_ = vertices_.data();
		numVertices_ = numVertices;
		renderCommand_.GetGeometry().SetVertexCount(numVertices);
		renderCommand_.GetGeometry().SetElementsPerVertex(numFloats);
		renderCommand_.GetGeometry().SetHostVertexPointer(vertexDataPointer_);

		dirtyBits_.set(DirtyBitPositions::SizeBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	void MeshSprite::createVerticesFromTexels(std::uint32_t numVertices, const Vector2f* points)
	{
		createVerticesFromTexels(numVertices, points, TextureCutMode::RESIZE);
	}

	void MeshSprite::copyIndices(std::uint32_t numIndices, const std::uint16_t* indices)
	{
		indices_.reserve(numIndices);
		std::memcpy(indices_.data(), indices, numIndices * sizeof(std::uint16_t));

		indexDataPointer_ = indices_.data();
		numIndices_ = numIndices;
		renderCommand_.GetGeometry().SetIndexCount(numIndices_);
		renderCommand_.GetGeometry().SetHostIndexPointer(indexDataPointer_);
	}

	void MeshSprite::copyIndices(const MeshSprite& meshSprite)
	{
		copyIndices(meshSprite.numIndices_, meshSprite.indexDataPointer_);
	}

	void MeshSprite::setIndices(std::uint32_t numIndices, const std::uint16_t* indices)
	{
		indices_.clear();

		indexDataPointer_ = indices;
		numIndices_ = numIndices;
		renderCommand_.GetGeometry().SetIndexCount(numIndices_);
		renderCommand_.GetGeometry().SetHostIndexPointer(indexDataPointer_);
	}

	void MeshSprite::setIndices(const MeshSprite& meshSprite)
	{
		setIndices(meshSprite.numIndices_, meshSprite.indexDataPointer_);
	}

	unsigned short* MeshSprite::emplaceIndices(std::uint32_t numIndices)
	{
		if (numIndices == 0) {
			return nullptr;
		}

		indices_.clear();
		indices_.resize(numIndices);

		indexDataPointer_ = indices_.data();
		numIndices_ = numIndices;
		renderCommand_.GetGeometry().SetIndexCount(numIndices_);
		renderCommand_.GetGeometry().SetHostIndexPointer(indexDataPointer_);

		return indices_.data();
	}

	MeshSprite::MeshSprite(const MeshSprite& other)
		: BaseSprite(other)
	{
		init();
		setTexRect(other.texRect_);
		copyVertices(other.numVertices_, other.bytesPerVertex_, other.vertices_.data());
		copyIndices(other.numIndices_, other.indices_.data());
	}

	void MeshSprite::init()
	{
		// TODO
		/*if (texture_ != nullptr && texture_->name() != nullptr) {
			// When Tracy is disabled the statement body is empty and braces are needed
			ZoneText(texture_->name(), nctl::strnlen(texture_->name(), Object::MaxNameLength));
		}*/

		_type = ObjectType::MeshSprite;
		renderCommand_.SetType(RenderCommand::Type::MeshSprite);

		const Material::ShaderProgramType shaderProgramType = (texture_ != nullptr ? Material::ShaderProgramType::MeshSprite : Material::ShaderProgramType::MeshSpriteNoTexture);
		renderCommand_.GetMaterial().SetShaderProgramType(shaderProgramType);

		shaderHasChanged();
		renderCommand_.GetGeometry().SetPrimitiveType(GL_TRIANGLE_STRIP);
		renderCommand_.GetGeometry().SetElementsPerVertex(texture_ ? VertexFloats : VertexNoTextureFloats);
		renderCommand_.GetGeometry().SetHostVertexPointer(vertexDataPointer_);

		if (texture_ != nullptr) {
			setTexRect(Recti(0, 0, texture_->GetWidth(), texture_->GetHeight()));
		}
	}

	void MeshSprite::shaderHasChanged()
	{
		BaseSprite::shaderHasChanged();
		renderCommand_.GetMaterial().SetDefaultAttributesParameters();
	}

	void MeshSprite::textureHasChanged(Texture* newTexture)
	{
		if (renderCommand_.GetMaterial().GetShaderProgramType() != Material::ShaderProgramType::Custom) {
			const Material::ShaderProgramType shaderProgramType = (newTexture != nullptr ? Material::ShaderProgramType::MeshSprite : Material::ShaderProgramType::MeshSpriteNoTexture);
			const bool hasChanged = renderCommand_.GetMaterial().SetShaderProgramType(shaderProgramType);
			if (hasChanged) {
				shaderHasChanged();
			}
			renderCommand_.GetGeometry().SetElementsPerVertex(newTexture ? VertexFloats : VertexNoTextureFloats);
		}

		if (texture_ != nullptr && newTexture != nullptr && texture_ != newTexture) {
			Recti texRect = texRect_;
			texRect.X = (texRect.X * newTexture->GetWidth() / texture_->GetWidth());
			texRect.Y = (texRect.Y * newTexture->GetHeight() / texture_->GetHeight());
			texRect.W = (texRect.W * newTexture->GetWidth() / texture_->GetWidth());
			texRect.H = (texRect.H * newTexture->GetHeight() / texture_->GetHeight());
			setTexRect(texRect); // it also sets width_ and height_
		} else if (texture_ == nullptr && newTexture != nullptr) {
			// Assigning a texture when there wasn't any
			setTexRect(Recti(0, 0, newTexture->GetWidth(), newTexture->GetHeight()));
		}
	}
}
