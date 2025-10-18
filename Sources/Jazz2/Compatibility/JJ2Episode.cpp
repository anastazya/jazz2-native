﻿#include "JJ2Episode.h"
#include "JJ2Anims.h"
#include "JJ2Anims.Palettes.h"
#include "../ContentResolver.h"

#include "../../nCine/Base/Algorithms.h"

#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

using namespace Death::IO;

namespace Jazz2::Compatibility
{
	JJ2Episode::JJ2Episode()
		: Position(0), ImageWidth(0), ImageHeight(0), TitleWidth(0), TitleHeight(0)
	{
	}

	JJ2Episode::JJ2Episode(StringView name, StringView displayName, StringView firstLevel, std::int32_t position)
		: Name(name), DisplayName(displayName), FirstLevel(firstLevel), Position(position), ImageWidth(0), ImageHeight(0), TitleWidth(0), TitleHeight(0)
	{
	}

	bool JJ2Episode::Open(StringView path)
	{
		auto s = fs::Open(path, FileAccess::Read);
		if (!s->IsValid()) {
			LOGE("Cannot open file \"{}\" for reading", path);
			return false;
		}

		Name = fs::GetFileNameWithoutExtension(path);
		StringUtils::lowercaseInPlace(Name);

		// TODO: Implement JJ2+ extended data, but I haven't seen it anywhere yet
		// the condition of unlocking (currently only defined for 0 meaning "always unlocked"
		// and 1 meaning "requires the previous episode to be finished", stored as a 4-byte-long
		// integer starting at byte 0x4), binary flags of various purpose (currently supported
		// flags are 1 and 2 used to reset respectively player ammo and lives when the episode
		// begins; stored as a 4-byte-long integer starting at byte 0x8)

		// Header (208 bytes)
		/*std::int32_t headerSize =*/ Stream::FromLE(s->ReadValue<std::int32_t>());
		Position = Stream::FromLE(s->ReadValue<std::int32_t>());
		/*std::uint32_t flags =*/ Stream::FromLE(s->ReadValue<std::uint32_t>());	// 0x01 = Not Shareware
		/*std::uint32_t unknown1 =*/ Stream::FromLE(s->ReadValue<std::uint32_t>());

		char tmpBuffer[64];

		// Episode name
		s->Read(tmpBuffer, 64);
		std::int32_t length = 0;
		while (tmpBuffer[length] != '\0' && length < 64) {
			length++;
		}
		DisplayName = String(tmpBuffer, length);

		// Previous Episode
		s->Read(tmpBuffer, 32);
		length = 0;
		while (tmpBuffer[length] != '\0' && length < 32) {
			length++;
		}
		PreviousEpisode = String(tmpBuffer, length);
		StringUtils::lowercaseInPlace(PreviousEpisode);

		// Next Episode
		s->Read(tmpBuffer, 32);
		length = 0;
		while (tmpBuffer[length] != '\0' && length < 32) {
			length++;
		}
		NextEpisode = String(tmpBuffer, length);
		StringUtils::lowercaseInPlace(NextEpisode);

		// First level
		s->Read(tmpBuffer, 32);
		length = 0;
		while (tmpBuffer[length] != '\0' && length < 32) {
			length++;
		}
		FirstLevel = String(tmpBuffer, length);
		StringUtils::lowercaseInPlace(FirstLevel);

		ImageWidth = Stream::FromLE(s->ReadValue<std::int32_t>());
		ImageHeight = Stream::FromLE(s->ReadValue<std::int32_t>());
		/*std::int32_t unknown2 =*/ Stream::FromLE(s->ReadValue<std::int32_t>());
		/*std::int32_t unknown3 =*/ Stream::FromLE(s->ReadValue<std::int32_t>());

		TitleWidth = Stream::FromLE(s->ReadValue<std::int32_t>());
		TitleHeight = Stream::FromLE(s->ReadValue<std::int32_t>());
		/*std::int32_t unknown4 =*/ Stream::FromLE(s->ReadValue<std::int32_t>());
		/*std::int32_t unknown5 =*/ Stream::FromLE(s->ReadValue<std::int32_t>());

		// Background image
		{
			std::int32_t imagePackedSize = Stream::FromLE(s->ReadValue<std::int32_t>());
			std::int32_t imageUnpackedSize = ImageWidth * ImageHeight;
			JJ2Block imageBlock(s, imagePackedSize, imageUnpackedSize);
			ImageData = std::make_unique<std::uint8_t[]>(imageUnpackedSize);
			imageBlock.ReadRawBytes(ImageData.get(), imageUnpackedSize);
		}

		// Title image
		{
			std::int32_t titleLightPackedSize = Stream::FromLE(s->ReadValue<std::int32_t>());
			std::int32_t titleLightUnpackedSize = TitleWidth * TitleHeight;
			JJ2Block titleLightBlock(s, titleLightPackedSize, titleLightUnpackedSize);
			TitleData = std::make_unique<std::uint8_t[]>(titleLightUnpackedSize);
			titleLightBlock.ReadRawBytes(TitleData.get(), titleLightUnpackedSize);
		}
		//{
		//    std::int32_t titleDarkPackedSize = Stream::FromLE(->ReadValue<std::int32_t>());
		//    std::int32_t titleDarkUnpackedSize = titleWidth * titleHeight;
		//    JJ2Block titleDarkBlock(s, titleDarkPackedSize, titleDarkUnpackedSize);
		//    episode.titleDark = ConvertIndicesToRgbaBitmap(titleWidth, titleHeight, titleDarkBlock, true);
		//}

		return true;
	}

	void JJ2Episode::Convert(StringView targetPath, Function<JJ2Level::LevelToken(StringView)>&& levelTokenConversion, Function<String(JJ2Episode*)>&& episodeNameConversion, Function<Pair<String, String>(JJ2Episode*)>&& episodePrevNext)
	{
		auto so = fs::Open(targetPath, FileAccess::Write);
		DEATH_ASSERT(so->IsValid(), "Cannot open file for writing", );

		so->WriteValue<std::uint64_t>(0x2095A59FF0BFBBEF);
		so->WriteValue<std::uint8_t>(ContentResolver::EpisodeFile);

		std::uint16_t flags = 0x00;
		so->WriteValue<std::uint16_t>(flags);

		String displayName = (episodeNameConversion ? episodeNameConversion(this) : DisplayName);
		so->WriteValue<std::uint8_t>((std::uint8_t)displayName.size());
		so->Write(displayName.data(), displayName.size());

		so->WriteValue<std::uint16_t>((std::uint16_t)Position);

		MutableStringView firstLevel = FirstLevel;
		if (JJ2Level::StringHasSuffixIgnoreCase(firstLevel, ".j2l"_s) ||
			JJ2Level::StringHasSuffixIgnoreCase(firstLevel, ".lev"_s)) {
			firstLevel = firstLevel.exceptSuffix(4);
		}

		if (levelTokenConversion) {
			auto token = levelTokenConversion(firstLevel);
			so->WriteValue<std::uint8_t>((std::uint8_t)token.Level.size());
			so->Write(token.Level.data(), token.Level.size());
		} else {
			so->WriteValue<std::uint8_t>((std::uint8_t)firstLevel.size());
			so->Write(firstLevel.data(), firstLevel.size());
		}

		if (!PreviousEpisode.empty() || !NextEpisode.empty()) {
			MutableStringView previousEpisode = PreviousEpisode;
			if (JJ2Level::StringHasSuffixIgnoreCase(previousEpisode, ".j2e"_s)) {
				previousEpisode = previousEpisode.exceptSuffix(4);
			} else if (JJ2Level::StringHasSuffixIgnoreCase(previousEpisode, ".j2pe"_s)) {
				previousEpisode = previousEpisode.exceptSuffix(5);
			}
			MutableStringView nextEpisode = NextEpisode;
			if (JJ2Level::StringHasSuffixIgnoreCase(nextEpisode, ".j2e"_s)) {
				nextEpisode = nextEpisode.exceptSuffix(4);
			} else if(JJ2Level::StringHasSuffixIgnoreCase(nextEpisode, ".j2pe"_s)) {
				nextEpisode = nextEpisode.exceptSuffix(5);
			}

			so->WriteValue<std::uint8_t>((std::uint8_t)previousEpisode.size());
			so->Write(previousEpisode.data(), previousEpisode.size());
			so->WriteValue<std::uint8_t>((std::uint8_t)nextEpisode.size());
			so->Write(nextEpisode.data(), nextEpisode.size());
		} else if (episodePrevNext) {
			auto prevNext = episodePrevNext(this);
			so->WriteValue<std::uint8_t>((std::uint8_t)prevNext.first().size());
			so->Write(prevNext.first().data(), prevNext.first().size());
			so->WriteValue<std::uint8_t>((std::uint8_t)prevNext.second().size());
			so->Write(prevNext.second().data(), prevNext.second().size());
		} else {
			so->WriteValue<std::uint8_t>(0);
			so->WriteValue<std::uint8_t>(0);
		}

		// Write episode title image
		so->WriteValue<std::uint16_t>((std::uint16_t)TitleWidth);
		so->WriteValue<std::uint16_t>((std::uint16_t)TitleHeight);

		std::uint32_t titlePixelsCount = TitleWidth * TitleHeight;
		std::unique_ptr<std::uint8_t[]> titlePixels = std::make_unique<std::uint8_t[]>(titlePixelsCount * 4);
		for (std::uint32_t i = 0; i < titlePixelsCount; i++) {
			std::uint8_t colorIdx = TitleData[i];

			// Remove shadow
			if (colorIdx == 63 || colorIdx == 143) {
				colorIdx = 0;
			}

			const Color& src = MenuPalette[colorIdx];
			titlePixels[i * 4] = src.R;
			titlePixels[i * 4 + 1] = src.G;
			titlePixels[i * 4 + 2] = src.B;
			titlePixels[i * 4 + 3] = src.A;
		}

		JJ2Anims::WriteImageContent(*so, titlePixels.get(), TitleWidth, TitleHeight, 4);

		// Write episode background image
		so->WriteValue<std::uint16_t>((std::uint16_t)ImageWidth);
		so->WriteValue<std::uint16_t>((std::uint16_t)ImageHeight);

		std::uint32_t imagePixelsCount = ImageWidth * ImageHeight;
		std::unique_ptr<std::uint8_t[]> imagePixels = std::make_unique<std::uint8_t[]>(imagePixelsCount * 4);
		for (std::uint32_t i = 0; i < imagePixelsCount; i++) {
			std::uint8_t colorIdx = ImageData[i];

			const Color& src = MenuPalette[colorIdx];
			imagePixels[i * 4] = src.R;
			imagePixels[i * 4 + 1] = src.G;
			imagePixels[i * 4 + 2] = src.B;
			imagePixels[i * 4 + 3] = src.A;
		}

		JJ2Anims::WriteImageContent(*so, imagePixels.get(), ImageWidth, ImageHeight, 4);
	}
}