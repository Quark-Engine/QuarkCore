/*
    ========================================================
    
        Quark Image Module
        By Quark Engine Development Team

    --------------------------------------------------------

    This file contains:
        * Image structure and management
        * Image loading and saving
        * Image manipulation and drawing functions
        * Image format conversion and compression

    ========================================================
*/

#ifndef __QUARK_IMAGE__
#define __QUARK_IMAGE__

#include <cstddef>
#include <cstdint>

struct Font;

/**
 * @brief Image pixel format enumeration (raylib compatible values).
 */
typedef enum {
    PIXELFORMAT_UNCOMPRESSED_GRAYSCALE = 1,     // 8 bit per pixel (no alpha)
    PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA,         // 8*2 bpp (2 channels)
    PIXELFORMAT_UNCOMPRESSED_R5G6B5,             // 16 bpp
    PIXELFORMAT_UNCOMPRESSED_R8G8B8,             // 24 bpp (3 channels)
    PIXELFORMAT_UNCOMPRESSED_R5G5B5A1,           // 16 bpp (1 bit alpha)
    PIXELFORMAT_UNCOMPRESSED_R4G4B4A4,           // 16 bpp (4 bit alpha)
    PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,           // 32 bpp (4 channels)
    PIXELFORMAT_UNCOMPRESSED_R8G8B8A8_SRGB,      // 32 bpp (4 channels, sRGB)
    PIXELFORMAT_UNCOMPRESSED_B5G6R5,             // 16 bpp
    PIXELFORMAT_UNCOMPRESSED_B5G5R5A1,           // 16 bpp (1 bit alpha)
    PIXELFORMAT_UNCOMPRESSED_B4G4R4A4,           // 16 bpp (4 bit alpha)
    PIXELFORMAT_UNCOMPRESSED_B8G8R8A8,           // 32 bpp (4 channels)
    PIXELFORMAT_UNCOMPRESSED_R32,                // 32 bpp (1 float channel)
    PIXELFORMAT_UNCOMPRESSED_R32G32B32,          // 32*3 bpp (3 float channels)
    PIXELFORMAT_UNCOMPRESSED_R32G32B32A32,       // 32*4 bpp (4 float channels)
    PIXELFORMAT_UNCOMPRESSED_R16,                // 16 bpp (1 half-float channel)
    PIXELFORMAT_UNCOMPRESSED_R16G16B16,          // 16*3 bpp (3 half-float channels)
    PIXELFORMAT_UNCOMPRESSED_R16G16B16A16,       // 16*4 bpp (4 half-float channels)
    PIXELFORMAT_UNCOMPRESSED_R8,                 // 8 bpp (1 channel)
    PIXELFORMAT_UNCOMPRESSED_R8G8,               // 8*2 bpp (2 channels)
    PIXELFORMAT_COMPRESSED_DXT1_RGB,             // 4 bpp (no alpha)
    PIXELFORMAT_COMPRESSED_DXT1_RGBA,            // 4 bpp (1 bit alpha)
    PIXELFORMAT_COMPRESSED_DXT3_RGBA,            // 8 bpp
    PIXELFORMAT_COMPRESSED_DXT5_RGBA,            // 8 bpp
    PIXELFORMAT_COMPRESSED_BC4_RUN,              // 4 bpp
    PIXELFORMAT_COMPRESSED_BC5_RG,               // 8 bpp
    PIXELFORMAT_COMPRESSED_BC6H_RGB,             // 8 bpp
    PIXELFORMAT_COMPRESSED_BC7_RGBA,             // 8 bpp
    PIXELFORMAT_COMPRESSED_ETC1_RGB,             // 4 bpp
    PIXELFORMAT_COMPRESSED_ETC2_RGB,             // 4 bpp
    PIXELFORMAT_COMPRESSED_ETC2_EAC_RGBA,        // 8 bpp
    PIXELFORMAT_COMPRESSED_PVRT_RGB,             // 4 bpp
    PIXELFORMAT_COMPRESSED_PVRT_RGBA,            // 4 bpp
    PIXELFORMAT_COMPRESSED_ASTC_4x4_RGBA,        // 8 bpp
    PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA         // 2 bpp
} PixelFormat;

/**
 * @brief CPU image structure. Raw pixel data owned by the caller.
 *        Release with UnloadImage(); buffer was allocated via MemAlloc().
 */
struct Image {
    void* data = nullptr;    // Image raw data
    int   width = 0;         // Image base width
    int   height = 0;        // Image base height
    int   mipmaps = 1;       // Mipmap levels, 1 by default
    int   format = 0;        // Data format (PixelFormat)
};

/**
 * @brief Load an image from a file into CPU memory.
 * @param fileName Path to the image file.
 * @return Loaded image.
 */
QCAPI Image           LoadImage(const char* fileName);

/**
 * @brief Load a raw (headerless) image from a file.
 * @param fileName Path to the raw image file.
 * @param width Image width, in pixels.
 * @param height Image height, in pixels.
 * @param format Pixel data format (PixelFormat).
 * @param headerSize Size of the file header to skip, in bytes.
 * @return Loaded image.
 */
QCAPI Image           LoadImageRaw(const char* fileName, int width, int height, int format, int headerSize);

/**
 * @brief Load the first frame of an animated image file (e.g. GIF).
 * @param fileName Path to the animated image file.
 * @param frames Output parameter receiving the total number of frames.
 * @return Loaded image (first frame).
 */
QCAPI Image           LoadImageAnim(const char* fileName, int* frames);

/**
 * @brief Load an image sequence from a memory buffer (e.g. animated GIF).
 * @param fileType File extension identifying the encoded format (e.g. ".gif").
 * @param fileData Pointer to the encoded image data in memory.
 * @param dataSize Size of the encoded image data, in bytes.
 * @param frames Output parameter receiving the total number of frames.
 * @return Loaded image with all frames appended vertically.
 */
QCAPI Image           LoadImageAnimFromMemory(const char* fileType, const unsigned char* fileData, int dataSize, int* frames);

/**
 * @brief Load an image from a memory buffer, given its file type extension.
 * @param fileType File extension identifying the encoded format (e.g. ".png").
 * @param fileData Pointer to the encoded image data in memory.
 * @param dataSize Size of the encoded image data, in bytes.
 * @return Loaded image.
 */
QCAPI Image           LoadImageFromMemory(const char* fileType, const unsigned char* fileData, int dataSize);

/**
 * @brief Load an image from GPU texture data into CPU memory.
 * @param texture Source texture.
 * @return Loaded image containing the texture's pixel data.
 */
QCAPI Image           LoadImageFromTexture(Texture2D texture);

/**
 * @brief Load an image from the current screen framebuffer (screenshot).
 * @return Loaded image containing the current screen contents.
 */
QCAPI Image           LoadImageFromScreen(void);

/**
 * @brief Check whether an image is valid (has data and correct dimensions/format).
 * @param image Image to validate.
 * @return True if the image is valid, false otherwise.
 */
QCAPI bool            IsImageValid(Image image);

/**
 * @brief Unload an image from CPU memory, freeing its pixel data.
 * @param image Image to unload.
 */
QCAPI void            UnloadImage(Image image);

/**
 * @brief Export an image to a file, encoding it based on the file extension.
 * @param image Image to export.
 * @param fileName Destination file path.
 * @return True if the export succeeded, false otherwise.
 */
QCAPI bool            ExportImage(Image image, const char* fileName);

/**
 * @brief Export an image to an encoded memory buffer.
 * @param image Image to export.
 * @param fileType File extension identifying the desired encoded format (e.g. ".png").
 * @param fileSize Output parameter receiving the size of the encoded buffer, in bytes.
 * @return Pointer to the encoded data buffer (caller owns and must free it).
 */
QCAPI unsigned char*  ExportImageToMemory(Image image, const char* fileType, int* fileSize);

/**
 * @brief Export an image as a C source code array (byte data).
 * @param image Image to export.
 * @param fileName Destination file path for the generated source file.
 * @return True if the export succeeded, false otherwise.
 */
QCAPI bool            ExportImageAsCode(Image image, const char* fileName);

/**
 * @brief Generate an image filled with a solid color.
 * @param width Image width, in pixels.
 * @param height Image height, in pixels.
 * @param color Fill color.
 * @return Generated image.
 */
QCAPI Image           GenImageColor(int width, int height, Color color);

/**
 * @brief Generate an image with a linear gradient.
 * @param width Image width, in pixels.
 * @param height Image height, in pixels.
 * @param direction Gradient direction, in degrees.
 * @param start Starting gradient color.
 * @param end Ending gradient color.
 * @return Generated image.
 */
QCAPI Image           GenImageGradientLinear(int width, int height, int direction, Color start, Color end);

/**
 * @brief Generate an image with a radial gradient.
 * @param width Image width, in pixels.
 * @param height Image height, in pixels.
 * @param density Gradient density, controlling the radius of the inner color.
 * @param inner Inner gradient color.
 * @param outer Outer gradient color.
 * @return Generated image.
 */
QCAPI Image           GenImageGradientRadial(int width, int height, float density, Color inner, Color outer);

/**
 * @brief Generate an image with a square gradient.
 * @param width Image width, in pixels.
 * @param height Image height, in pixels.
 * @param density Gradient density, controlling the size of the inner color square.
 * @param inner Inner gradient color.
 * @param outer Outer gradient color.
 * @return Generated image.
 */
QCAPI Image           GenImageGradientSquare(int width, int height, float density, Color inner, Color outer);

/**
 * @brief Generate an image with a checkerboard pattern.
 * @param width Image width, in pixels.
 * @param height Image height, in pixels.
 * @param checksX Number of checker tiles along the X axis.
 * @param checksY Number of checker tiles along the Y axis.
 * @param col1 First checker color.
 * @param col2 Second checker color.
 * @return Generated image.
 */
QCAPI Image           GenImageChecked(int width, int height, int checksX, int checksY, Color col1, Color col2);

/**
 * @brief Generate an image filled with white noise.
 * @param width Image width, in pixels.
 * @param height Image height, in pixels.
 * @param factor Noise factor, controlling the proportion of white pixels (0.0f-1.0f).
 * @return Generated image.
 */
QCAPI Image           GenImageWhiteNoise(int width, int height, float factor);

/**
 * @brief Generate an image filled with Perlin noise.
 * @param width Image width, in pixels.
 * @param height Image height, in pixels.
 * @param offsetX Noise sampling offset along the X axis.
 * @param offsetY Noise sampling offset along the Y axis.
 * @param scale Noise sampling scale.
 * @return Generated image.
 */
QCAPI Image           GenImagePerlinNoise(int width, int height, int offsetX, int offsetY, float scale);

/**
 * @brief Generate an image with a cellular (Worley) noise pattern.
 * @param width Image width, in pixels.
 * @param height Image height, in pixels.
 * @param tileSize Size of each cellular tile, in pixels.
 * @return Generated image.
 */
QCAPI Image           GenImageCellular(int width, int height, int tileSize);

/**
 * @brief Generate an image containing the given text, using the default font.
 * @param width Image width, in pixels.
 * @param height Image height, in pixels.
 * @param text Text to render into the image.
 * @return Generated image.
 */
QCAPI Image           GenImageText(int width, int height, const char* text);

/**
 * @brief Render text into a new image using the default font.
 * @param text Text to render.
 * @param fontSize Font size, in pixels.
 * @param color Text color.
 * @return Generated image containing the rendered text.
 */
QCAPI Image           ImageText(const char* text, int fontSize, Color color);

/**
 * @brief Render text into a new image using a custom font.
 * @param font Font to use for rendering.
 * @param text Text to render.
 * @param fontSize Font size, in pixels.
 * @param spacing Extra spacing between characters, in pixels.
 * @param tint Text tint color.
 * @return Generated image containing the rendered text.
 */
QCAPI Image           ImageTextEx(Font font, const char* text, float fontSize, float spacing, Color tint);

/**
 * @brief Create a deep copy of an image.
 * @param image Source image.
 * @return Copy of the image, with its own pixel data.
 */
QCAPI Image           ImageCopy(Image image);

/**
 * @brief Create a new image from a rectangular region of an existing image.
 * @param image Source image.
 * @param rec Rectangle defining the region to extract, in pixels.
 * @return New image containing the extracted region.
 */
QCAPI Image           ImageFromImage(Image image, Rectangle rec);
QCAPI Image           ImageFromChannel(Image image, int selectedChannel);

/**
 * @brief Convert an image's pixel data to a new format, in place.
 * @param image Pointer to the image to convert.
 * @param newFormat Target pixel format (PixelFormat).
 */
QCAPI void            ImageFormat(Image* image, int newFormat);

/**
 * @brief Convert an image to power-of-two dimensions, padding with a fill color.
 * @param image Pointer to the image to convert.
 * @param fill Color used to fill the padded area.
 */
QCAPI void            ImageToPOT(Image* image, Color fill);

/**
 * @brief Crop an image to a given rectangle, in place.
 * @param image Pointer to the image to crop.
 * @param crop Rectangle defining the region to keep, in pixels.
 */
QCAPI void            ImageCrop(Image* image, Rectangle crop);

/**
 * @brief Crop an image to its non-transparent (alpha) bounds, in place.
 * @param image Pointer to the image to crop.
 * @param threshold Alpha threshold below which pixels are considered transparent (0.0f-1.0f).
 */
QCAPI void            ImageAlphaCrop(Image* image, float threshold);

/**
 * @brief Clear pixels matching a given color to transparent, in place.
 * @param image Pointer to the image to modify.
 * @param color Color to clear.
 * @param threshold Color similarity threshold for matching (0.0f-1.0f).
 */
QCAPI void            ImageAlphaClear(Image* image, Color color, float threshold);

/**
 * @brief Apply an alpha mask to an image, in place.
 * @param image Pointer to the image to modify.
 * @param alphaMask Grayscale image used as the alpha mask.
 */
QCAPI void            ImageAlphaMask(Image* image, Image alphaMask);

/**
 * @brief Premultiply an image's color channels by its alpha channel, in place.
 * @param image Pointer to the image to modify.
 */
QCAPI void            ImageAlphaPremultiply(Image* image);

/**
 * @brief Apply a Gaussian blur to an image, in place.
 * @param image Pointer to the image to blur.
 * @param blurSize Size of the blur kernel, in pixels.
 */
QCAPI void            ImageBlurGaussian(Image* image, int blurSize);
QCAPI void            ImageKernelConvolution(Image* image, const float* kernel, int kernelSize);

/**
 * @brief Resize an image using bicubic scaling, in place.
 * @param image Pointer to the image to resize.
 * @param newWidth Target width, in pixels.
 * @param newHeight Target height, in pixels.
 */
QCAPI void            ImageResize(Image* image, int newWidth, int newHeight);

/**
 * @brief Resize an image using nearest-neighbor scaling, in place.
 * @param image Pointer to the image to resize.
 * @param newWidth Target width, in pixels.
 * @param newHeight Target height, in pixels.
 */
QCAPI void            ImageResizeNN(Image* image, int newWidth, int newHeight);

/**
 * @brief Resize an image's canvas, offsetting the original content and filling new area, in place.
 * @param image Pointer to the image to resize.
 * @param newWidth Target canvas width, in pixels.
 * @param newHeight Target canvas height, in pixels.
 * @param offsetX Horizontal offset of the original image within the new canvas, in pixels.
 * @param offsetY Vertical offset of the original image within the new canvas, in pixels.
 * @param fill Color used to fill the newly added canvas area.
 */
QCAPI void            ImageResizeCanvas(Image* image, int newWidth, int newHeight, int offsetX, int offsetY, Color fill);

/**
 * @brief Generate all mipmap levels for an image, in place.
 * @param image Pointer to the image to process.
 */
QCAPI void            ImageMipmaps(Image* image);

/**
 * @brief Dither an image to a reduced per-channel bit depth, in place.
 * @param image Pointer to the image to dither.
 * @param rBpp Bits per pixel for the red channel.
 * @param gBpp Bits per pixel for the green channel.
 * @param bBpp Bits per pixel for the blue channel.
 * @param aBpp Bits per pixel for the alpha channel.
 */
QCAPI void            ImageDither(Image* image, int rBpp, int gBpp, int bBpp, int aBpp);

/**
 * @brief Flip an image vertically, in place.
 * @param image Pointer to the image to flip.
 */
QCAPI void            ImageFlipVertical(Image* image);

/**
 * @brief Flip an image horizontally, in place.
 * @param image Pointer to the image to flip.
 */
QCAPI void            ImageFlipHorizontal(Image* image);

/**
 * @brief Rotate an image by an arbitrary angle, in place.
 * @param image Pointer to the image to rotate.
 * @param degrees Rotation angle, in degrees.
 */
QCAPI void            ImageRotate(Image* image, int degrees);

/**
 * @brief Rotate an image 90 degrees clockwise, in place.
 * @param image Pointer to the image to rotate.
 */
QCAPI void            ImageRotateCW(Image* image);

/**
 * @brief Rotate an image 90 degrees counter-clockwise, in place.
 * @param image Pointer to the image to rotate.
 */
QCAPI void            ImageRotateCCW(Image* image);

/**
 * @brief Apply a color tint to an image, in place.
 * @param image Pointer to the image to modify.
 * @param color Tint color.
 */
QCAPI void            ImageColorTint(Image* image, Color color);

/**
 * @brief Invert an image's colors, in place.
 * @param image Pointer to the image to modify.
 */
QCAPI void            ImageColorInvert(Image* image);

/**
 * @brief Convert an image to grayscale, in place.
 * @param image Pointer to the image to modify.
 */
QCAPI void            ImageColorGrayscale(Image* image);

/**
 * @brief Adjust an image's contrast, in place.
 * @param image Pointer to the image to modify.
 * @param contrast Contrast value, in the range -100 to 100.
 */
QCAPI void            ImageColorContrast(Image* image, int contrast);

/**
 * @brief Adjust an image's brightness, in place.
 * @param image Pointer to the image to modify.
 * @param brightness Brightness value, in the range -255 to 255.
 */
QCAPI void            ImageColorBrightness(Image* image, int brightness);

/**
 * @brief Replace all occurrences of a color in an image with another color, in place.
 * @param image Pointer to the image to modify.
 * @param color Color to replace.
 * @param replace Replacement color.
 */
QCAPI void            ImageColorReplace(Image* image, Color color, Color replace);

/**
 * @brief Clear an image's contents with a solid color.
 * @param dst Pointer to the destination image.
 * @param color Fill color.
 */
QCAPI void            ImageClearBackground(Image* dst, Color color);

/**
 * @brief Draw a source image onto a destination image, with scaling and tinting.
 * @param dst Pointer to the destination image.
 * @param src Source image to draw.
 * @param srcRec Source rectangle to draw from, in pixels.
 * @param dstRec Destination rectangle to draw into, in pixels.
 * @param tint Tint color applied to the source image.
 */
QCAPI void            ImageDraw(Image* dst, Image src, Rectangle srcRec, Rectangle dstRec, Color tint);

/**
 * @brief Draw a filled rectangle onto an image.
 * @param dst Pointer to the destination image.
 * @param posX X coordinate of the top-left corner, in pixels.
 * @param posY Y coordinate of the top-left corner, in pixels.
 * @param width Rectangle width, in pixels.
 * @param height Rectangle height, in pixels.
 * @param color Fill color.
 */
QCAPI void            ImageDrawRectangle(Image* dst, int posX, int posY, int width, int height, Color color);

/**
 * @brief Draw a filled rectangle onto an image, using vector position and size.
 * @param dst Pointer to the destination image.
 * @param position Top-left position of the rectangle, in pixels.
 * @param size Size of the rectangle, in pixels.
 * @param color Fill color.
 */
QCAPI void            ImageDrawRectangleV(Image* dst, Vec2 position, Vec2 size, Color color);

/**
 * @brief Draw a filled rectangle onto an image.
 * @param dst Pointer to the destination image.
 * @param rec Rectangle to draw, in pixels.
 * @param color Fill color.
 */
QCAPI void            ImageDrawRectangleRec(Image* dst, Rectangle rec, Color color);

/**
 * @brief Draw a filled rectangle onto an image with rotation.
 * @param dst Pointer to the destination image.
 * @param rec Rectangle to draw, in pixels.
 * @param origin Rotation origin point, relative to the rectangle's top-left corner.
 * @param rotation Rotation angle, in degrees.
 * @param color Fill color.
 */
QCAPI void            ImageDrawRectanglePro(Image* dst, Rectangle rec, Vec2 origin, float rotation, Color color);

/**
 * @brief Draw a rectangle outline onto an image.
 * @param dst Pointer to the destination image.
 * @param posX X coordinate of the top-left corner, in pixels.
 * @param posY Y coordinate of the top-left corner, in pixels.
 * @param width Rectangle width, in pixels.
 * @param height Rectangle height, in pixels.
 * @param color Line color.
 */
QCAPI void            ImageDrawRectangleLines(Image* dst, int posX, int posY, int width, int height, Color color);

/**
 * @brief Draw a rectangle outline onto an image with line thickness.
 * @param dst Pointer to the destination image.
 * @param rec Rectangle to draw, in pixels.
 * @param thick Line thickness, in pixels.
 * @param color Line color.
 */
QCAPI void            ImageDrawRectangleLinesEx(Image* dst, Rectangle rec, int thick, Color color);

/**
 * @brief Draw a rectangle with gradient colors onto an image (counter-clockwise color order).
 * @param dst Pointer to the destination image.
 * @param posX X coordinate of the top-left corner, in pixels.
 * @param posY Y coordinate of the top-left corner, in pixels.
 * @param width Rectangle width, in pixels.
 * @param height Rectangle height, in pixels.
 * @param col1 Top-left gradient color.
 * @param col2 Top-right gradient color.
 * @param col3 Bottom-right gradient color.
 * @param col4 Bottom-left gradient color.
 */
QCAPI void            ImageDrawRectangleGradient(Image* dst, int posX, int posY, int width, int height, Color col1, Color col2, Color col3, Color col4);

/**
 * @brief Draw a rectangle with gradient colors onto an image (counter-clockwise color order).
 * @param dst Pointer to the destination image.
 * @param rec Rectangle to draw, in pixels.
 * @param col1 Top-left gradient color.
 * @param col2 Top-right gradient color.
 * @param col3 Bottom-right gradient color.
 * @param col4 Bottom-left gradient color.
 */
QCAPI void            ImageDrawRectangleGradientEx(Image* dst, Rectangle rec, Color col1, Color col2, Color col3, Color col4);

/**
 * @brief Draw a filled circle onto an image.
 * @param dst Pointer to the destination image.
 * @param centerX X coordinate of the circle center, in pixels.
 * @param centerY Y coordinate of the circle center, in pixels.
 * @param radius Circle radius, in pixels.
 * @param color Fill color.
 */
QCAPI void            ImageDrawCircle(Image* dst, int centerX, int centerY, int radius, Color color);

/**
 * @brief Draw a filled circle onto an image, using a vector center position.
 * @param dst Pointer to the destination image.
 * @param center Circle center position, in pixels.
 * @param radius Circle radius, in pixels.
 * @param color Fill color.
 */
QCAPI void            ImageDrawCircleV(Image* dst, Vec2 center, int radius, Color color);

/**
 * @brief Draw a circle outline onto an image.
 * @param dst Pointer to the destination image.
 * @param centerX X coordinate of the circle center, in pixels.
 * @param centerY Y coordinate of the circle center, in pixels.
 * @param radius Circle radius, in pixels.
 * @param color Line color.
 */
QCAPI void            ImageDrawCircleLines(Image* dst, int centerX, int centerY, int radius, Color color);

/**
 * @brief Draw a circle outline onto an image, using a vector center position.
 * @param dst Pointer to the destination image.
 * @param center Circle center position, in pixels.
 * @param radius Circle radius, in pixels.
 * @param color Line color.
 */
QCAPI void            ImageDrawCircleLinesV(Image* dst, Vec2 center, int radius, Color color);

/**
 * @brief Draw a circle with gradient colors onto an image.
 * @param dst Pointer to the destination image.
 * @param center Circle center position, in pixels.
 * @param radius Circle radius, in pixels.
 * @param inner Inner gradient color.
 * @param outer Outer gradient color.
 */
QCAPI void            ImageDrawCircleGradient(Image* dst, Vec2 center, float radius, Color inner, Color outer);

/**
 * @brief Draw a line segment onto an image.
 * @param dst Pointer to the destination image.
 * @param startPosX X coordinate of the line's start point, in pixels.
 * @param startPosY Y coordinate of the line's start point, in pixels.
 * @param endPosX X coordinate of the line's end point, in pixels.
 * @param endPosY Y coordinate of the line's end point, in pixels.
 * @param color Line color.
 */
QCAPI void            ImageDrawLine(Image* dst, int startPosX, int startPosY, int endPosX, int endPosY, Color color);

/**
 * @brief Draw a line segment onto an image, using vector start and end points.
 * @param dst Pointer to the destination image.
 * @param start Line start point, in pixels.
 * @param end Line end point, in pixels.
 * @param color Line color.
 */
QCAPI void            ImageDrawLineV(Image* dst, Vec2 start, Vec2 end, Color color);

/**
 * @brief Draw a line segment onto an image with line thickness.
 * @param dst Pointer to the destination image.
 * @param start Line start point, in pixels.
 * @param end Line end point, in pixels.
 * @param thick Line thickness, in pixels.
 * @param color Line color.
 */
QCAPI void            ImageDrawLineEx(Image* dst, Vec2 start, Vec2 end, int thick, Color color);

/**
 * @brief Draw a sequence of connected line segments onto an image.
 * @param dst Pointer to the destination image.
 * @param points Array of points defining the line strip.
 * @param count Number of points in the array.
 * @param color Line color.
 */
QCAPI void            ImageDrawLineStrip(Image* dst, Vec2* points, int count, Color color);

/**
 * @brief Draw a filled triangle onto an image.
 * @param dst Pointer to the destination image.
 * @param v1 First vertex, in pixels.
 * @param v2 Second vertex, in pixels.
 * @param v3 Third vertex, in pixels.
 * @param color Fill color.
 */
QCAPI void            ImageDrawTriangle(Image* dst, Vec2 v1, Vec2 v2, Vec2 v3, Color color);

/**
 * @brief Draw a filled triangle onto an image with per-vertex gradient colors.
 * @param dst Pointer to the destination image.
 * @param v1 First vertex, in pixels.
 * @param v2 Second vertex, in pixels.
 * @param v3 Third vertex, in pixels.
 * @param c1 Color of the first vertex.
 * @param c2 Color of the second vertex.
 * @param c3 Color of the third vertex.
 */
QCAPI void            ImageDrawTriangleGradient(Image* dst, Vec2 v1, Vec2 v2, Vec2 v3, Color c1, Color c2, Color c3);

/**
 * @brief Draw a triangle outline onto an image.
 * @param dst Pointer to the destination image.
 * @param v1 First vertex, in pixels.
 * @param v2 Second vertex, in pixels.
 * @param v3 Third vertex, in pixels.
 * @param color Line color.
 */
QCAPI void            ImageDrawTriangleLines(Image* dst, Vec2 v1, Vec2 v2, Vec2 v3, Color color);

/**
 * @brief Draw a triangle fan onto an image (connected triangles, first vertex is the shared center).
 * @param dst Pointer to the destination image.
 * @param points Array of points defining the triangle fan.
 * @param count Number of points in the array.
 * @param color Fill color.
 */
QCAPI void            ImageDrawTriangleFan(Image* dst, Vec2* points, int count, Color color);

/**
 * @brief Draw a triangle strip onto an image (connected triangles).
 * @param dst Pointer to the destination image.
 * @param points Array of points defining the triangle strip.
 * @param count Number of points in the array.
 * @param color Fill color.
 */
QCAPI void            ImageDrawTriangleStrip(Image* dst, Vec2* points, int count, Color color);

/**
 * @brief Draw a single pixel onto an image.
 * @param dst Pointer to the destination image.
 * @param posX X coordinate of the pixel, in pixels.
 * @param posY Y coordinate of the pixel, in pixels.
 * @param color Pixel color.
 */
QCAPI void            ImageDrawPixel(Image* dst, int posX, int posY, Color color);

/**
 * @brief Draw a single pixel onto an image, using a vector position.
 * @param dst Pointer to the destination image.
 * @param position Pixel position, in pixels.
 * @param color Pixel color.
 */
QCAPI void            ImageDrawPixelV(Image* dst, Vec2 position, Color color);

/**
 * @brief Draw text onto an image using the default font.
 * @param dst Pointer to the destination image.
 * @param text Text to draw.
 * @param posX X coordinate of the text's top-left corner, in pixels.
 * @param posY Y coordinate of the text's top-left corner, in pixels.
 * @param fontSize Font size, in pixels.
 * @param color Text color.
 */
QCAPI void            ImageDrawText(Image* dst, const char* text, int posX, int posY, int fontSize, Color color);

/**
 * @brief Draw text onto an image using a custom font.
 * @param dst Pointer to the destination image.
 * @param font Font to use for drawing.
 * @param text Text to draw.
 * @param position Position of the text's top-left corner, in pixels.
 * @param fontSize Font size, in pixels.
 * @param spacing Extra spacing between characters, in pixels.
 * @param tint Text tint color.
 */
QCAPI void            ImageDrawTextEx(Image* dst, Font font, const char* text, Vec2 position, float fontSize, float spacing, Color tint);

/**
 * @brief Draw text onto an image using a custom font, with rotation around an origin.
 * @param dst Pointer to the destination image.
 * @param font Font to use for drawing.
 * @param text Text to draw.
 * @param position Position of the text's origin point, in pixels.
 * @param origin Rotation origin point, relative to the text position, in pixels.
 * @param rotation Rotation angle, in degrees.
 * @param fontSize Font size, in pixels.
 * @param spacing Extra spacing between characters, in pixels.
 * @param tint Text tint color.
 */
QCAPI void            ImageDrawTextPro(Image* dst, Font font, const char* text, Vec2 position, Vec2 origin, float rotation, float fontSize, float spacing, Color tint);

/**
 * @brief Draw a source image onto a destination image, with scaling, rotation, and tinting.
 * @param dst Pointer to the destination image.
 * @param src Source image to draw.
 * @param srcRec Source rectangle to draw from, in pixels.
 * @param dstRec Destination rectangle to draw into, in pixels.
 * @param origin Rotation origin point, relative to the destination rectangle, in pixels.
 * @param rotation Rotation angle, in degrees.
 * @param tint Tint color applied to the source image.
 */
QCAPI void            ImageDrawImagePro(Image* dst, Image src, Rectangle srcRec, Rectangle dstRec, Vec2 origin, float rotation, Color tint);

/**
 * @brief Draw a source image onto a destination image.
 * @param dst Pointer to the destination image.
 * @param src Source image to draw.
 * @param posX X coordinate of the destination position, in pixels.
 * @param posY Y coordinate of the destination position, in pixels.
 * @param tint Tint color applied to the source image.
 */
QCAPI void            ImageDrawImage(Image* dst, Image src, int posX, int posY, Color tint);

/**
 * @brief Draw a source image onto a destination image, with scaling and rotation.
 * @param dst Pointer to the destination image.
 * @param src Source image to draw.
 * @param position Destination position, in pixels.
 * @param rotation Rotation angle, in degrees.
 * @param scale Scaling factor applied to the source image.
 * @param tint Tint color applied to the source image.
 */
QCAPI void            ImageDrawImageEx(Image* dst, Image src, Vec2 position, float rotation, float scale, Color tint);

/**
 * @brief Draw a source image onto a destination image, with region selection.
 * @param dst Pointer to the destination image.
 * @param src Source image to draw.
 * @param srcRec Source rectangle to draw from, in pixels.
 * @param dstRec Destination rectangle to draw into, in pixels.
 * @param tint Tint color applied to the source image.
 */
QCAPI void            ImageDrawImageRec(Image* dst, Image src, Rectangle srcRec, Rectangle dstRec, Color tint);

/**
 * @brief Load all pixel colors of an image into an array.
 * @param image Source image.
 * @return Pointer to an array of colors, one per pixel (caller owns and must free it).
 */
QCAPI Color*          LoadImageColors(Image image);

/**
 * @brief Load a reduced color palette from an image.
 * @param image Source image.
 * @param maxPaletteSize Maximum number of colors to extract.
 * @param colorCount Output parameter receiving the number of colors actually extracted.
 * @return Pointer to an array of unique colors (caller owns and must free it).
 */
QCAPI Color*          LoadImagePalette(Image image, int maxPaletteSize, int* colorCount);

/**
 * @brief Free color data loaded with LoadImageColors().
 * @param colors Pointer to the color array to free.
 */
QCAPI void            UnloadImageColors(Color* colors);

/**
 * @brief Free a color palette loaded with LoadImagePalette().
 * @param colors Pointer to the palette to free.
 */
QCAPI void            UnloadImagePalette(Color* colors);

/**
 * @brief Get the bounding rectangle of an image's non-transparent (alpha) area.
 * @param image Source image.
 * @param threshold Alpha threshold below which pixels are considered transparent (0.0f-1.0f).
 * @return Rectangle bounding the non-transparent pixels, in pixels.
 */
QCAPI Rectangle       GetImageAlphaBorder(Image image, float threshold);

/**
 * @brief Get the color of a specific pixel in an image.
 * @param image Source image.
 * @param x X coordinate of the pixel, in pixels.
 * @param y Y coordinate of the pixel, in pixels.
 * @return Color of the pixel at the given coordinates.
 */
QCAPI Color           GetImageColor(Image image, int x, int y);

#endif // __QUARK_IMAGE__