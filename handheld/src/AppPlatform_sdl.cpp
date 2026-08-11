#include "AppPlatform_sdl.h"
#include "util/Mth.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
#include <png.h>

// Declares getRemainingFileSize(); needs FILE to already be visible.
#include "world/level/storage/FolderMethods.h"

static void png_funcReadFile(png_structp pngPtr, png_bytep data, png_size_t length) {
	((std::istream*)png_get_io_ptr(pngPtr))->read((char*)data, length);
}

BinaryBlob AppPlatform_sdl::readAssetFile(const std::string& filename)
{
	const std::string path = _dataDir + "/" + filename;

	FILE* fp = fopen(path.c_str(), "rb");
	if (!fp) {
		LOGI("Couldn't find asset: %s\n", path.c_str());
		return BinaryBlob();
	}

	const int size = getRemainingFileSize(fp);

	BinaryBlob blob;
	blob.size = size;
	blob.data = new unsigned char[size];

	fread(blob.data, 1, size, fp);
	fclose(fp);

	return blob;
}

TextureData AppPlatform_sdl::loadTexture(const std::string& filename_, bool textureFolder)
{
	TextureData out;

	const std::string filename = textureFolder ? _dataDir + "/images/" + filename_
											   : filename_;
	std::ifstream source(filename.c_str(), std::ios::binary);

	if (!source) {
		LOGI("Couldn't find file: %s\n", filename.c_str());
		return out;
	}

	png_structp pngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!pngPtr)
		return out;

	png_infop infoPtr = png_create_info_struct(pngPtr);
	if (!infoPtr) {
		png_destroy_read_struct(&pngPtr, NULL, NULL);
		return out;
	}

	if (setjmp(png_jmpbuf(pngPtr))) {
		png_destroy_read_struct(&pngPtr, &infoPtr, (png_infopp)0);
		LOGE("libpng failed while reading: %s\n", filename.c_str());
		return TextureData();
	}

	png_set_read_fn(pngPtr, (png_voidp)&source, png_funcReadFile);
	png_read_info(pngPtr, infoPtr);

	out.w = png_get_image_width(pngPtr, infoPtr);
	out.h = png_get_image_height(pngPtr, infoPtr);

	// The renderer expects tightly packed RGBA8888 regardless of how the file
	// was actually encoded, so normalise palette / grayscale / 16-bit / no-alpha
	// variants up front rather than trusting every asset to already be RGBA.
	const png_byte colorType = png_get_color_type(pngPtr, infoPtr);
	const png_byte bitDepth  = png_get_bit_depth(pngPtr, infoPtr);

	if (colorType == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(pngPtr);
	if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
		png_set_expand_gray_1_2_4_to_8(pngPtr);
	if (png_get_valid(pngPtr, infoPtr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(pngPtr);
	if (bitDepth == 16)
		png_set_strip_16(pngPtr);
	if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(pngPtr);
	// Opaque alpha for anything that has no alpha channel of its own.
	png_set_filler(pngPtr, 0xFF, PNG_FILLER_AFTER);

	png_read_update_info(pngPtr, infoPtr);

	png_bytep* rowPtrs = new png_bytep[out.h];
	out.data = new unsigned char[4 * out.w * out.h];
	out.memoryHandledExternally = false;

	const int rowStrideBytes = 4 * out.w;
	for (int i = 0; i < out.h; i++)
		rowPtrs[i] = (png_bytep)&out.data[i * rowStrideBytes];

	png_read_image(pngPtr, rowPtrs);

	png_destroy_read_struct(&pngPtr, &infoPtr, (png_infopp)0);
	delete[] (png_bytep)rowPtrs;
	source.close();

	return out;
}

void AppPlatform_sdl::saveScreenshot(const std::string& filename, int glWidth, int glHeight)
{
	if (glWidth <= 0 || glHeight <= 0)
		return;

	std::vector<unsigned char> pixels(4 * glWidth * glHeight);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, glWidth, glHeight, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[0]);

	FILE* fp = fopen(filename.c_str(), "wb");
	if (!fp) {
		LOGE("Couldn't open screenshot for writing: %s\n", filename.c_str());
		return;
	}

	png_structp pngPtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!pngPtr) {
		fclose(fp);
		return;
	}

	png_infop infoPtr = png_create_info_struct(pngPtr);
	if (!infoPtr) {
		png_destroy_write_struct(&pngPtr, NULL);
		fclose(fp);
		return;
	}

	if (setjmp(png_jmpbuf(pngPtr))) {
		png_destroy_write_struct(&pngPtr, &infoPtr);
		fclose(fp);
		return;
	}

	png_init_io(pngPtr, fp);
	png_set_IHDR(pngPtr, infoPtr, glWidth, glHeight, 8, PNG_COLOR_TYPE_RGBA,
				 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(pngPtr, infoPtr);

	// glReadPixels hands back bottom-up rows; png wants them top-down.
	for (int y = glHeight - 1; y >= 0; --y)
		png_write_row(pngPtr, (png_bytep)&pixels[4 * glWidth * y]);

	png_write_end(pngPtr, NULL);
	png_destroy_write_struct(&pngPtr, &infoPtr);
	fclose(fp);

	LOGI("Saved screenshot: %s\n", filename.c_str());
}

std::string AppPlatform_sdl::getDateString(int s)
{
	std::stringstream ss;
	ss << s << " s (UTC)";
	return ss.str();
}

std::string AppPlatform_sdl::getPlatformStringVar(int stringId)
{
	if (stringId == PlatformStringVars::DEVICE_BUILD_MODEL)
#if defined(__EMSCRIPTEN__)
		return "Browser";
#else
		return "Macintosh";
#endif
	return "";
}

float AppPlatform_sdl::getPixelsPerMillimeter()
{
	// Roughly a 27" desktop display; only used to size touch targets, which
	// this build doesn't show anyway.
	const int w = 2560;
	const int h = 1440;
	const float pixels = Mth::sqrt((float)(w * w + h * h));
	const float mm     = 27 * 25.4f;
	return pixels / mm;
}
