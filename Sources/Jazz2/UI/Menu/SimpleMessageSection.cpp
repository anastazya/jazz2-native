﻿#include "SimpleMessageSection.h"
#include "MenuResources.h"

#include "../../../nCine/I18n.h"

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	SimpleMessageSection::SimpleMessageSection(StringView message, bool withTransition)
		: _message(message), _transitionTime(withTransition ? 0.0f : 1.0f)
	{
	}

	SimpleMessageSection::SimpleMessageSection(String&& message, bool withTransition)
		: _message(std::move(message)), _transitionTime(withTransition ? 0.0f : 1.0f)
	{
	}

	void SimpleMessageSection::OnUpdate(float timeMult)
	{
		if (_transitionTime < 1.0f) {
			_transitionTime += 0.025f * timeMult;
		}

		if (_root->ActionHit(PlayerAction::Menu) || _root->ActionHit(PlayerAction::Fire)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		}
	}

	void SimpleMessageSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		Vector2f center = Vector2f(contentBounds.X + contentBounds.W * 0.5f, contentBounds.Y + contentBounds.H * 0.5f);
		float topLine = contentBounds.Y + 31.0f;
		float bottomLine = contentBounds.Y + contentBounds.H - 50.0f;

		_root->DrawElement(MenuDim, center.X, topLine - 2.0f, IMenuContainer::BackgroundLayer,
			Alignment::Top, Colorf::Black, Vector2f(680.0f, 200.0f), Vector4f(1.0f, 0.0f, -0.7f, 0.7f));
		_root->DrawElement(MenuLine, 0, center.X, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;
		_root->DrawStringShadow(_message, charOffset, center.X, topLine - 30.0f, IMenuContainer::FontLayer,
			Alignment::Top, Font::DefaultColor, 0.9f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);

		_root->DrawStringShadow(_("Press \f[c:#d0705d]Fire\f[/c] to continue"), charOffset, center.X, bottomLine, IMenuContainer::FontLayer,
			Alignment::Bottom, Font::DefaultColor, 0.9f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);
	}

	void SimpleMessageSection::OnDrawOverlay(Canvas* canvas)
	{
		if (_transitionTime < 1.0f) {
			Vector2i viewSize = canvas->ViewSize;

			auto* command = canvas->RentRenderCommand();
			if (command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::Transition))) {
				command->GetMaterial().ReserveUniformsDataMemory();
				command->GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			}

			command->GetMaterial().SetBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			auto* instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatVector(Vector4f(1.0f, 0.0f, 1.0f, 0.0f).Data());
			instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(Vector2f(static_cast<float>(viewSize.X), static_cast<float>(viewSize.Y)).Data());
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf(0.0f, 0.0f, 0.0f, _transitionTime).Data());

			command->SetTransformation(Matrix4x4f::Identity);
			command->SetLayer(999);

			canvas->DrawRenderCommand(command);
		}
	}

	void SimpleMessageSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
				if (y < 80.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
				}
			}
		}
	}
}