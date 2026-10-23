#include "sources/bpm_display_source.hpp"

#include "heart_rate/heart_rate_state.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#endif

namespace {

struct BpmDisplaySource {
	obs_source_t *source = nullptr;
	uint32_t width = 360;
	uint32_t height = 160;
	uint32_t color = 0xFFFFFFFF;
	uint32_t resting_color = 0xFF38D878;
	uint32_t active_color = 0xFF47A8FF;
	uint32_t high_color = 0xFF00D7FF;
	uint32_t peak_color = 0xFF3B3BFF;
	int render_mode = 0;
	int digit_style = 0;
	std::string font_face = "Helvetica Neue";
	int font_size = 96;
	int font_flags = OBS_FONT_BOLD;
	bool use_zone_colors = false;
	bool use_smoothed = true;
	bool show_when_disconnected = true;
	bool glow_enabled = false;
	double glow_softness = 6.0;
	bool outline_enabled = false;
	uint32_t outline_color = 0xFF000000;
	int outline_size = 4;
	double active_bpm = 100.0;
	double high_bpm = 130.0;
	double peak_bpm = 160.0;
	bool displayed_bpm_initialized = false;
	double displayed_bpm = 0.0;
	std::chrono::steady_clock::time_point last_animation_frame = std::chrono::steady_clock::now();
	gs_texture_t *text_texture = nullptr;
	std::string cached_text;
	uint32_t cached_color = 0;
	uint32_t cached_width = 0;
	uint32_t cached_height = 0;
	std::string cached_font_face;
	int cached_font_size = 0;
	int cached_font_flags = 0;
	uint32_t cached_outline_color = 0;
	int cached_outline_size = 0;
	bool cached_font_valid = false;
};

enum RenderMode {
	RenderModeSystemFont = 0,
	RenderModeSegments = 1,
};

enum DigitStyle {
	DigitStyleDigital = 0,
	DigitStyleSlim = 1,
	DigitStyleBold = 2,
	DigitStyleBlock = 3,
};

enum Segment {
	SegmentTop = 1 << 0,
	SegmentUpperRight = 1 << 1,
	SegmentLowerRight = 1 << 2,
	SegmentBottom = 1 << 3,
	SegmentLowerLeft = 1 << 4,
	SegmentUpperLeft = 1 << 5,
	SegmentMiddle = 1 << 6,
};

const uint8_t kDigitSegments[] = {
	SegmentTop | SegmentUpperRight | SegmentLowerRight | SegmentBottom | SegmentLowerLeft | SegmentUpperLeft,
	SegmentUpperRight | SegmentLowerRight,
	SegmentTop | SegmentUpperRight | SegmentMiddle | SegmentLowerLeft | SegmentBottom,
	SegmentTop | SegmentUpperRight | SegmentMiddle | SegmentLowerRight | SegmentBottom,
	SegmentUpperLeft | SegmentMiddle | SegmentUpperRight | SegmentLowerRight,
	SegmentTop | SegmentUpperLeft | SegmentMiddle | SegmentLowerRight | SegmentBottom,
	SegmentTop | SegmentUpperLeft | SegmentMiddle | SegmentLowerLeft | SegmentLowerRight | SegmentBottom,
	SegmentTop | SegmentUpperRight | SegmentLowerRight,
	SegmentTop | SegmentUpperRight | SegmentLowerRight | SegmentBottom | SegmentLowerLeft | SegmentUpperLeft |
		SegmentMiddle,
	SegmentTop | SegmentUpperRight | SegmentLowerRight | SegmentBottom | SegmentUpperLeft | SegmentMiddle,
};

const char *bpm_display_get_name(void *)
{
	return obs_module_text("Source.BpmDisplay.Name");
}

void bpm_display_update(void *data, obs_data_t *settings)
{
	auto *display = static_cast<BpmDisplaySource *>(data);
	display->width = (uint32_t)std::max(120LL, obs_data_get_int(settings, "width"));
	display->height = (uint32_t)std::max(80LL, obs_data_get_int(settings, "height"));
	display->color = (uint32_t)obs_data_get_int(settings, "color");
	display->resting_color = (uint32_t)obs_data_get_int(settings, "resting_color");
	display->active_color = (uint32_t)obs_data_get_int(settings, "active_color");
	display->high_color = (uint32_t)obs_data_get_int(settings, "high_color");
	display->peak_color = (uint32_t)obs_data_get_int(settings, "peak_color");
	display->render_mode = (int)obs_data_get_int(settings, "render_mode");
	display->digit_style = (int)obs_data_get_int(settings, "digit_style");
	if (obs_data_t *font = obs_data_get_obj(settings, "font")) {
		const char *face = obs_data_get_string(font, "face");
		display->font_face = (face && *face) ? face : "Helvetica Neue";
		display->font_size = (int)std::clamp(obs_data_get_int(font, "size"), 8LL, 512LL);
		display->font_flags = (int)obs_data_get_int(font, "flags");
		obs_data_release(font);
	}
	display->use_zone_colors = obs_data_get_bool(settings, "use_zone_colors");
	display->use_smoothed = obs_data_get_bool(settings, "use_smoothed");
	display->show_when_disconnected = obs_data_get_bool(settings, "show_when_disconnected");
	display->glow_enabled = obs_data_get_bool(settings, "glow_enabled");
	display->glow_softness = std::clamp(obs_data_get_double(settings, "glow_softness"), 1.0, 32.0);
	display->outline_enabled = obs_data_get_bool(settings, "outline_enabled");
	display->outline_color = (uint32_t)obs_data_get_int(settings, "outline_color");
	display->outline_size = (int)std::clamp(obs_data_get_int(settings, "outline_size"), 1LL, 32LL);
	display->active_bpm = std::clamp(obs_data_get_double(settings, "active_bpm"), 1.0, 300.0);
	display->high_bpm = std::clamp(obs_data_get_double(settings, "high_bpm"), display->active_bpm + 1.0, 300.0);
	display->peak_bpm = std::clamp(obs_data_get_double(settings, "peak_bpm"), display->high_bpm + 1.0, 300.0);
	display->cached_font_valid = false;
}

void *bpm_display_create(obs_data_t *settings, obs_source_t *source)
{
	auto *display = new BpmDisplaySource;
	display->source = source;
	bpm_display_update(display, settings);
	return display;
}

void bpm_display_destroy(void *data)
{
	auto *display = static_cast<BpmDisplaySource *>(data);
	if (display->text_texture)
		gs_texture_destroy(display->text_texture);
	delete display;
}

void draw_rect(float x, float y, float width, float height, float grow = 0.0f)
{
	gs_matrix_push();
	gs_matrix_translate3f(x - grow, y - grow, 0.0f);
	gs_draw_sprite(nullptr, 0, (uint32_t)(width + (2.0f * grow)), (uint32_t)(height + (2.0f * grow)));
	gs_matrix_pop();
}

void draw_digit(uint8_t segments, float x, float y, float digit_width, float digit_height, float thickness,
		float grow)
{
	const float horizontal_width = digit_width - (2.0f * thickness);
	const float vertical_height = (digit_height - (3.0f * thickness)) * 0.5f;
	const float right = x + digit_width - thickness;
	const float mid_y = y + vertical_height + thickness;
	const float bottom_y = y + digit_height - thickness;

	if (segments & SegmentTop)
		draw_rect(x + thickness, y, horizontal_width, thickness, grow);
	if (segments & SegmentMiddle)
		draw_rect(x + thickness, mid_y, horizontal_width, thickness, grow);
	if (segments & SegmentBottom)
		draw_rect(x + thickness, bottom_y, horizontal_width, thickness, grow);
	if (segments & SegmentUpperLeft)
		draw_rect(x, y + thickness, thickness, vertical_height, grow);
	if (segments & SegmentUpperRight)
		draw_rect(right, y + thickness, thickness, vertical_height, grow);
	if (segments & SegmentLowerLeft)
		draw_rect(x, mid_y + thickness, thickness, vertical_height, grow);
	if (segments & SegmentLowerRight)
		draw_rect(right, mid_y + thickness, thickness, vertical_height, grow);
}

void digit_metrics(int style, float digit_height, float *digit_width, float *thickness, float *spacing)
{
	switch (style) {
	case DigitStyleSlim:
		*digit_width = digit_height * 0.50f;
		*thickness = std::max(3.0f, digit_height * 0.065f);
		*spacing = *digit_width * 0.22f;
		break;
	case DigitStyleBold:
		*digit_width = digit_height * 0.62f;
		*thickness = std::max(5.0f, digit_height * 0.145f);
		*spacing = *digit_width * 0.16f;
		break;
	case DigitStyleBlock:
		*digit_width = digit_height * 0.70f;
		*thickness = std::max(6.0f, digit_height * 0.20f);
		*spacing = *digit_width * 0.11f;
		break;
	case DigitStyleDigital:
	default:
		*digit_width = digit_height * 0.56f;
		*thickness = std::max(4.0f, digit_height * 0.105f);
		*spacing = *digit_width * 0.20f;
		break;
	}
}

void draw_dash(float x, float y, float digit_width, float digit_height, float thickness, float grow)
{
	const float horizontal_width = digit_width - (2.0f * thickness);
	const float mid_y = y + ((digit_height - thickness) * 0.5f);
	draw_rect(x + thickness, mid_y, horizontal_width, thickness, grow);
}

struct DisplayValue {
	std::string text;
	bool has_bpm = false;
	double bpm = 0.0;
};

double animated_display_bpm(BpmDisplaySource *display, double target_bpm)
{
	const auto now = std::chrono::steady_clock::now();
	double dt = std::chrono::duration<double>(now - display->last_animation_frame).count();
	display->last_animation_frame = now;
	dt = std::clamp(dt, 0.0, 0.20);

	const double rounded_target = std::clamp(std::lround(target_bpm), 0l, 999l);
	if (!display->displayed_bpm_initialized) {
		display->displayed_bpm = rounded_target;
		display->displayed_bpm_initialized = true;
		return display->displayed_bpm;
	}

	constexpr double bpm_per_second = 24.0;
	const double difference = rounded_target - display->displayed_bpm;
	const double max_step = bpm_per_second * dt;
	if (std::abs(difference) <= max_step) {
		display->displayed_bpm = rounded_target;
	} else {
		display->displayed_bpm += (difference > 0.0 ? max_step : -max_step);
	}

	return display->displayed_bpm;
}

DisplayValue display_value(BpmDisplaySource *display)
{
	const auto snapshot = hyperate::heart_rate_state().snapshot();
	if (!snapshot.has_sample || !snapshot.is_live) {
		display->displayed_bpm_initialized = false;
		display->last_animation_frame = std::chrono::steady_clock::now();
		return DisplayValue{display->show_when_disconnected ? "--" : "", false, 0.0};
	}

	const double bpm = display->use_smoothed ? snapshot.smoothed_bpm : snapshot.raw_bpm;
	const double animated_bpm = animated_display_bpm(display, bpm);
	const int rounded = std::clamp((int)std::lround(animated_bpm), 0, 999);
	return DisplayValue{std::to_string(rounded), true, animated_bpm};
}

uint32_t display_color(const BpmDisplaySource *display, const DisplayValue &value)
{
	if (!display->use_zone_colors || !value.has_bpm)
		return display->color;

	if (value.bpm >= display->peak_bpm)
		return display->peak_color;
	if (value.bpm >= display->high_bpm)
		return display->high_color;
	if (value.bpm >= display->active_bpm)
		return display->active_color;
	return display->resting_color;
}

void invalidate_text_texture(BpmDisplaySource *display)
{
	if (display->text_texture) {
		gs_texture_destroy(display->text_texture);
		display->text_texture = nullptr;
	}
	display->cached_font_valid = false;
}

int effective_outline_size(const BpmDisplaySource *display)
{
	return display->outline_enabled ? display->outline_size : 0;
}

// Felzenszwalb & Huttenlocher 1D squared distance transform.
void distance_transform_1d(const float *f, int n, float *d, int *v, float *z)
{
	constexpr float inf = 1e20f;
	int k = 0;
	v[0] = 0;
	z[0] = -inf;
	z[1] = inf;
	for (int q = 1; q < n; ++q) {
		float s = ((f[q] + (float)(q * q)) - (f[v[k]] + (float)(v[k] * v[k]))) / (float)(2 * q - 2 * v[k]);
		while (s <= z[k]) {
			--k;
			s = ((f[q] + (float)(q * q)) - (f[v[k]] + (float)(v[k] * v[k]))) / (float)(2 * q - 2 * v[k]);
		}
		++k;
		v[k] = q;
		z[k] = s;
		z[k + 1] = inf;
	}
	k = 0;
	for (int q = 0; q < n; ++q) {
		while (z[k + 1] < (float)q)
			++k;
		const float delta = (float)(q - v[k]);
		d[q] = (delta * delta) + f[v[k]];
	}
}

std::vector<uint8_t> outline_coverage(const std::vector<uint8_t> &mask, int width, int height, int radius)
{
	constexpr float inf = 1e20f;
	const int longest = std::max(width, height);
	std::vector<float> grid((size_t)width * (size_t)height);
	for (size_t i = 0; i < grid.size(); ++i)
		grid[i] = mask[i] >= 128 ? 0.0f : inf;

	std::vector<float> f((size_t)longest);
	std::vector<float> d((size_t)longest);
	std::vector<int> v((size_t)longest);
	std::vector<float> z((size_t)longest + 1);

	for (int x = 0; x < width; ++x) {
		for (int y = 0; y < height; ++y)
			f[(size_t)y] = grid[(size_t)y * width + x];
		distance_transform_1d(f.data(), height, d.data(), v.data(), z.data());
		for (int y = 0; y < height; ++y)
			grid[(size_t)y * width + x] = d[(size_t)y];
	}
	for (int y = 0; y < height; ++y) {
		float *row = grid.data() + (size_t)y * width;
		std::copy(row, row + width, f.begin());
		distance_transform_1d(f.data(), width, d.data(), v.data(), z.data());
		std::copy(d.begin(), d.begin() + width, row);
	}

	std::vector<uint8_t> coverage(grid.size());
	for (size_t i = 0; i < grid.size(); ++i) {
		const float value = std::clamp((float)radius + 1.0f - std::sqrt(grid[i]), 0.0f, 1.0f);
		coverage[i] = std::max(mask[i], (uint8_t)std::lround(value * 255.0f));
	}
	return coverage;
}

void create_text_texture(BpmDisplaySource *display, const std::string &text, uint32_t color,
			 const std::vector<uint8_t> &mask)
{
	const int width = (int)display->width;
	const int height = (int)display->height;
	const int outline_size = effective_outline_size(display);
	std::vector<uint8_t> outline;
	if (outline_size > 0)
		outline = outline_coverage(mask, width, height, outline_size);

	const float text_rgb[] = {(float)((color >> 16) & 0xFF), (float)((color >> 8) & 0xFF),
				  (float)((color >> 0) & 0xFF)};
	const float text_alpha = (float)((color >> 24) & 0xFF) / 255.0f;
	const uint32_t oc = display->outline_color;
	const float outline_rgb[] = {(float)((oc >> 16) & 0xFF), (float)((oc >> 8) & 0xFF), (float)((oc >> 0) & 0xFF)};
	const float outline_alpha = (float)((oc >> 24) & 0xFF) / 255.0f;

	std::vector<uint8_t> pixels(mask.size() * 4, 0);
	for (size_t i = 0; i < mask.size(); ++i) {
		const float ta = text_alpha * ((float)mask[i] / 255.0f);
		const float oa = outline.empty() ? 0.0f : outline_alpha * ((float)outline[i] / 255.0f) * (1.0f - ta);
		const float alpha = ta + oa;
		if (alpha <= 0.0f)
			continue;
		for (int c = 0; c < 3; ++c) {
			const float value = ((text_rgb[c] * ta) + (outline_rgb[c] * oa)) / alpha;
			pixels[(i * 4) + c] = (uint8_t)std::clamp(std::lround(value), 0l, 255l);
		}
		pixels[(i * 4) + 3] = (uint8_t)std::clamp(std::lround(alpha * 255.0f), 0l, 255l);
	}

	const uint8_t *texture_data = pixels.data();
	display->text_texture = gs_texture_create(display->width, display->height, GS_BGRA, 1, &texture_data, 0);
	display->cached_font_valid = display->text_texture != nullptr;
	display->cached_text = text;
	display->cached_color = color;
	display->cached_width = display->width;
	display->cached_height = display->height;
	display->cached_font_face = display->font_face;
	display->cached_font_size = display->font_size;
	display->cached_font_flags = display->font_flags;
	display->cached_outline_color = display->outline_color;
	display->cached_outline_size = outline_size;
}

void ensure_system_font_texture(BpmDisplaySource *display, const std::string &text, uint32_t color)
{
	if (display->cached_font_valid && display->text_texture && display->cached_text == text &&
	    display->cached_color == color && display->cached_width == display->width &&
	    display->cached_height == display->height && display->cached_font_face == display->font_face &&
	    display->cached_font_size == display->font_size && display->cached_font_flags == display->font_flags &&
	    display->cached_outline_color == display->outline_color &&
	    display->cached_outline_size == effective_outline_size(display)) {
		return;
	}

	invalidate_text_texture(display);
	const int outline_margin = effective_outline_size(display) * 2;

#ifdef __APPLE__
	if (display->width == 0 || display->height == 0 || text.empty())
		return;

	std::vector<uint8_t> pixels((size_t)display->width * (size_t)display->height * 4, 0);

	CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
	CGContextRef context =
		CGBitmapContextCreate(pixels.data(), display->width, display->height, 8, display->width * 4,
				      color_space, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	CGColorSpaceRelease(color_space);
	if (!context)
		return;

	CGContextClearRect(context, CGRectMake(0, 0, display->width, display->height));
	CGContextSetShouldAntialias(context, true);
	CGContextSetAllowsAntialiasing(context, true);

	// Render a white coverage mask; color and outline are composited afterwards.
	CGColorRef text_color = CGColorCreateGenericRGB(1.0, 1.0, 1.0, 1.0);
	CFStringRef text_string = CFStringCreateWithCString(kCFAllocatorDefault, text.c_str(), kCFStringEncodingUTF8);

	auto create_font = [&](CGFloat size) -> CTFontRef {
		CFStringRef face = CFStringCreateWithCString(kCFAllocatorDefault, display->font_face.c_str(),
							     kCFStringEncodingUTF8);
		CTFontRef base_font = CTFontCreateWithName(face, size, nullptr);
		if (face)
			CFRelease(face);
		if (!base_font)
			return nullptr;

		CTFontSymbolicTraits traits = 0;
		if (display->font_flags & OBS_FONT_BOLD)
			traits |= kCTFontBoldTrait;
		if (display->font_flags & OBS_FONT_ITALIC)
			traits |= kCTFontItalicTrait;

		CTFontRef font =
			traits ? CTFontCreateCopyWithSymbolicTraits(base_font, size, nullptr, traits, traits)
			       : (CTFontRef)CFRetain(base_font);
		CFRelease(base_font);
		return font;
	};

	auto create_line = [&](CTFontRef font) -> CTLineRef {
		const void *keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
		const void *values[] = {font, text_color};
		CFDictionaryRef attrs =
			CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
					   &kCFTypeDictionaryValueCallBacks);
		CFAttributedStringRef attr_string = CFAttributedStringCreate(kCFAllocatorDefault, text_string, attrs);
		CTLineRef line = CTLineCreateWithAttributedString(attr_string);
		CFRelease(attr_string);
		CFRelease(attrs);
		return line;
	};

	const CGFloat max_text_width = std::max((CGFloat)1.0, (CGFloat)display->width * 0.92f - outline_margin);
	const CGFloat max_text_height = std::max((CGFloat)1.0, (CGFloat)display->height * 0.84f - outline_margin);
	CGFloat effective_font_size = std::min((CGFloat)display->font_size, std::max((CGFloat)8.0, max_text_height));
	CTFontRef font = create_font(effective_font_size);
	if (!font) {
		CFRelease(text_string);
		CGColorRelease(text_color);
		CGContextRelease(context);
		return;
	}

	CTLineRef line = create_line(font);
	CGFloat ascent = 0.0f;
	CGFloat descent = 0.0f;
	CGFloat leading = 0.0f;
	double line_width = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
	double line_height = ascent + descent;

	if ((line_width > max_text_width || line_height > max_text_height) && line_width > 0.0 && line_height > 0.0) {
		const double width_scale = (double)max_text_width / line_width;
		const double height_scale = (double)max_text_height / line_height;
		effective_font_size = (CGFloat)std::max(8.0, (double)effective_font_size *
							     std::min(width_scale, height_scale));
		CFRelease(line);
		CFRelease(font);
		font = create_font(effective_font_size);
		if (!font) {
			CFRelease(text_string);
			CGColorRelease(text_color);
			CGContextRelease(context);
			return;
		}
		line = create_line(font);
		line_width = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
		line_height = ascent + descent;
	}

	CGRect ink_bounds = CTLineGetImageBounds(line, context);
	const double x = ((double)display->width - CGRectGetWidth(ink_bounds)) * 0.5 - CGRectGetMinX(ink_bounds);
	const double y = ((double)display->height - CGRectGetHeight(ink_bounds)) * 0.5 - CGRectGetMinY(ink_bounds);

	// OBS uploads the bitmap rows directly as a texture, so draw in bitmap coordinates instead of flipping the
	// CoreText context like an AppKit view would.
	CGContextSetTextMatrix(context, CGAffineTransformIdentity);
	CGContextSetTextPosition(context, (CGFloat)x, (CGFloat)y);
	CTLineDraw(line, context);

	CFRelease(line);
	CFRelease(text_string);
	CGColorRelease(text_color);
	CFRelease(font);
	CGContextRelease(context);

	std::vector<uint8_t> mask((size_t)display->width * (size_t)display->height);
	for (size_t i = 0; i < mask.size(); ++i)
		mask[i] = pixels[(i * 4) + 3];
	create_text_texture(display, text, color, mask);
#elif defined(_WIN32)
	if (display->width == 0 || display->height == 0 || text.empty())
		return;

	const int width = (int)display->width;
	const int height = (int)display->height;

	HDC screen_dc = GetDC(nullptr);
	if (!screen_dc)
		return;

	HDC memory_dc = CreateCompatibleDC(screen_dc);
	if (!memory_dc) {
		ReleaseDC(nullptr, screen_dc);
		return;
	}

	BITMAPINFO bitmap_info = {};
	bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bitmap_info.bmiHeader.biWidth = width;
	bitmap_info.bmiHeader.biHeight = -height;
	bitmap_info.bmiHeader.biPlanes = 1;
	bitmap_info.bmiHeader.biBitCount = 32;
	bitmap_info.bmiHeader.biCompression = BI_RGB;

	void *bitmap_bits = nullptr;
	HBITMAP bitmap = CreateDIBSection(screen_dc, &bitmap_info, DIB_RGB_COLORS, &bitmap_bits, nullptr, 0);
	ReleaseDC(nullptr, screen_dc);
	if (!bitmap || !bitmap_bits) {
		DeleteDC(memory_dc);
		return;
	}

	HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);
	RECT full_rect{0, 0, width, height};
	HBRUSH black_brush = (HBRUSH)GetStockObject(BLACK_BRUSH);
	FillRect(memory_dc, &full_rect, black_brush);
	SetBkMode(memory_dc, TRANSPARENT);
	SetTextColor(memory_dc, RGB(255, 255, 255));

	auto utf8_to_wide = [](const std::string &value) {
		if (value.empty())
			return std::wstring();

		int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
		if (count <= 0)
			return std::wstring();

		std::wstring output((size_t)count, L'\0');
		if (!MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, output.data(), count))
			return std::wstring();
		if (!output.empty() && output.back() == L'\0')
			output.pop_back();
		return output;
	};

	const std::wstring wide_text = utf8_to_wide(text);
	if (wide_text.empty()) {
		SelectObject(memory_dc, old_bitmap);
		DeleteObject(bitmap);
		DeleteDC(memory_dc);
		return;
	}

	std::wstring wide_face = utf8_to_wide(display->font_face);
	if (wide_face.empty())
		wide_face = L"Arial";

	auto create_font = [&](int pixel_size) {
		LOGFONTW font = {};
		font.lfHeight = -std::max(8, pixel_size);
		font.lfWeight = (display->font_flags & OBS_FONT_BOLD) ? FW_BOLD : FW_NORMAL;
		font.lfItalic = (display->font_flags & OBS_FONT_ITALIC) ? TRUE : FALSE;
		font.lfCharSet = DEFAULT_CHARSET;
		font.lfQuality = ANTIALIASED_QUALITY;
		wcsncpy_s(font.lfFaceName, wide_face.c_str(), _TRUNCATE);
		return CreateFontIndirectW(&font);
	};

	auto measure_text = [&](HFONT font, RECT *rect) {
		HGDIOBJ old_font = SelectObject(memory_dc, font);
		*rect = RECT{0, 0, width, height};
		DrawTextW(memory_dc, wide_text.c_str(), -1, rect, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
		SelectObject(memory_dc, old_font);
	};

	const int max_text_width = std::max(1, (int)((double)width * 0.92) - outline_margin);
	const int max_text_height = std::max(1, (int)((double)height * 0.84) - outline_margin);
	int effective_font_size = std::min(display->font_size, max_text_height);
	HFONT font = create_font(effective_font_size);
	if (!font) {
		SelectObject(memory_dc, old_bitmap);
		DeleteObject(bitmap);
		DeleteDC(memory_dc);
		return;
	}

	RECT measured{};
	measure_text(font, &measured);
	const int measured_width = std::max(1, (int)(measured.right - measured.left));
	const int measured_height = std::max(1, (int)(measured.bottom - measured.top));
	if (measured_width > max_text_width || measured_height > max_text_height) {
		const double width_scale = (double)max_text_width / (double)measured_width;
		const double height_scale = (double)max_text_height / (double)measured_height;
		effective_font_size =
			std::max(8, (int)std::floor((double)effective_font_size * std::min(width_scale, height_scale)));
		DeleteObject(font);
		font = create_font(effective_font_size);
		if (!font) {
			SelectObject(memory_dc, old_bitmap);
			DeleteObject(bitmap);
			DeleteDC(memory_dc);
			return;
		}
	}

	HGDIOBJ old_font = SelectObject(memory_dc, font);
	DrawTextW(memory_dc, wide_text.c_str(), -1, &full_rect,
		  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	SelectObject(memory_dc, old_font);
	DeleteObject(font);

	const auto *source = static_cast<const uint8_t *>(bitmap_bits);
	std::vector<uint8_t> mask((size_t)width * (size_t)height);
	for (size_t i = 0; i < mask.size(); ++i)
		mask[i] = std::max(source[(i * 4) + 0], std::max(source[(i * 4) + 1], source[(i * 4) + 2]));

	SelectObject(memory_dc, old_bitmap);
	DeleteObject(bitmap);
	DeleteDC(memory_dc);

	create_text_texture(display, text, color, mask);
#else
	UNUSED_PARAMETER(display);
	UNUSED_PARAMETER(text);
	UNUSED_PARAMETER(color);
	UNUSED_PARAMETER(outline_margin);
#endif
}

void draw_system_font(BpmDisplaySource *display, const std::string &text, uint32_t color)
{
	ensure_system_font_texture(display, text, color);
	if (!display->text_texture)
		return;

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture_srgb(image, display->text_texture);

	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(display->text_texture, 0, display->width, display->height);
}

void draw_system_font_glow(BpmDisplaySource *display, const std::string &text, uint32_t color)
{
	ensure_system_font_texture(display, text, color);
	if (!display->text_texture)
		return;

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture_srgb(image, display->text_texture);

	const float radius = (float)display->glow_softness;
	const float inner = std::max(1.0f, radius * 0.45f);
	const float outer = std::max(inner + 1.0f, radius);
	const float offsets[][2] = {
		{-inner, 0.0f}, {inner, 0.0f}, {0.0f, -inner}, {0.0f, inner},
		{-inner, -inner}, {inner, -inner}, {-inner, inner}, {inner, inner},
		{-outer, 0.0f}, {outer, 0.0f}, {0.0f, -outer}, {0.0f, outer},
		{-outer, -outer}, {outer, -outer}, {-outer, outer}, {outer, outer},
	};

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_ONE);
	for (const auto &offset : offsets) {
		gs_matrix_push();
		gs_matrix_translate3f(offset[0], offset[1], 0.0f);
		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(display->text_texture, 0, display->width, display->height);
		gs_matrix_pop();
	}
	gs_blend_state_pop();
}

void draw_segment_text(BpmDisplaySource *display, const std::string &text, uint32_t color_rgba, float grow = 0.0f)
{
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

	vec4 color;
	vec4_from_rgba(&color, color_rgba);
	gs_effect_set_vec4(color_param, &color);

	const float canvas_width = (float)display->width;
	const float canvas_height = (float)display->height;
	const float digit_height = canvas_height * 0.78f;
	float digit_width = 0.0f;
	float thickness = 0.0f;
	float spacing = 0.0f;
	digit_metrics(display->digit_style, digit_height, &digit_width, &thickness, &spacing);
	const float total_width = (digit_width * (float)text.size()) + (spacing * (float)(text.size() - 1));
	const float start_x = (canvas_width - total_width) * 0.5f;
	const float start_y = (canvas_height - digit_height) * 0.5f;

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	for (size_t i = 0; i < text.size(); ++i) {
		const float x = start_x + ((digit_width + spacing) * (float)i);
		if (text[i] >= '0' && text[i] <= '9')
			draw_digit(kDigitSegments[text[i] - '0'], x, start_y, digit_width, digit_height, thickness, grow);
		else
			draw_dash(x, start_y, digit_width, digit_height, thickness, grow);
	}

	gs_technique_end_pass(tech);
	gs_technique_end(tech);
}

uint32_t with_alpha(uint32_t color, uint8_t alpha)
{
	return (color & 0x00FFFFFF) | ((uint32_t)alpha << 24);
}

void draw_segment_glow(BpmDisplaySource *display, const std::string &text, uint32_t color_rgba)
{
	const float radius = (float)display->glow_softness;
	const float inner = std::max(1.0f, radius * 0.45f);
	const float outer = std::max(inner + 1.0f, radius);
	const struct {
		float x;
		float y;
		uint8_t alpha;
	} offsets[] = {
		{-inner, 0.0f, 0x6A}, {inner, 0.0f, 0x6A}, {0.0f, -inner, 0x6A}, {0.0f, inner, 0x6A},
		{-inner, -inner, 0x52}, {inner, -inner, 0x52}, {-inner, inner, 0x52}, {inner, inner, 0x52},
		{-outer, 0.0f, 0x38}, {outer, 0.0f, 0x38}, {0.0f, -outer, 0x38}, {0.0f, outer, 0x38},
		{-outer, -outer, 0x28}, {outer, -outer, 0x28}, {-outer, outer, 0x28}, {outer, outer, 0x28},
	};

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_ONE);
	for (const auto &offset : offsets) {
		gs_matrix_push();
		gs_matrix_translate3f(offset.x, offset.y, 0.0f);
		draw_segment_text(display, text, with_alpha(color_rgba, offset.alpha));
		gs_matrix_pop();
	}
	gs_blend_state_pop();
}

void bpm_display_render(void *data, gs_effect_t *)
{
	auto *display = static_cast<BpmDisplaySource *>(data);
	const DisplayValue value = display_value(display);
	const std::string &text = value.text;
	if (text.empty())
		return;

	const uint32_t color = display_color(display, value);
	const int outline_size = effective_outline_size(display);
	auto draw_segments = [&]() {
		if (display->glow_enabled)
			draw_segment_glow(display, text, color);
		if (outline_size > 0)
			draw_segment_text(display, text, display->outline_color, (float)outline_size);
		draw_segment_text(display, text, color);
	};

	if (display->render_mode == RenderModeSystemFont) {
		if (display->glow_enabled)
			draw_system_font_glow(display, text, color);
		draw_system_font(display, text, color);
		if (!display->text_texture)
			draw_segments();
	} else {
		draw_segments();
	}
}

bool render_mode_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool system_font = obs_data_get_int(settings, "render_mode") == RenderModeSystemFont;
	obs_property_set_visible(obs_properties_get(props, "font"), system_font);
	obs_property_set_visible(obs_properties_get(props, "digit_style"), !system_font);
	return true;
}

bool glow_enabled_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	obs_property_set_visible(obs_properties_get(props, "glow_softness"), obs_data_get_bool(settings, "glow_enabled"));
	return true;
}

bool outline_enabled_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool enabled = obs_data_get_bool(settings, "outline_enabled");
	obs_property_set_visible(obs_properties_get(props, "outline_color"), enabled);
	obs_property_set_visible(obs_properties_get(props, "outline_size"), enabled);
	return true;
}

obs_properties_t *bpm_display_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *render_mode = obs_properties_add_list(props, "render_mode",
							      obs_module_text("Source.BpmDisplay.RenderMode"),
							      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(render_mode, obs_module_text("Source.BpmDisplay.RenderMode.SystemFont"),
				  RenderModeSystemFont);
	obs_property_list_add_int(render_mode, obs_module_text("Source.BpmDisplay.RenderMode.Segments"),
				  RenderModeSegments);
	obs_property_set_modified_callback(render_mode, render_mode_changed);

	obs_properties_add_font(props, "font", obs_module_text("Source.BpmDisplay.Font"));

	obs_property_t *style = obs_properties_add_list(props, "digit_style",
							 obs_module_text("Source.BpmDisplay.DigitStyle"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(style, obs_module_text("Source.BpmDisplay.DigitStyle.Digital"),
				  DigitStyleDigital);
	obs_property_list_add_int(style, obs_module_text("Source.BpmDisplay.DigitStyle.Slim"), DigitStyleSlim);
	obs_property_list_add_int(style, obs_module_text("Source.BpmDisplay.DigitStyle.Bold"), DigitStyleBold);
	obs_property_list_add_int(style, obs_module_text("Source.BpmDisplay.DigitStyle.Block"), DigitStyleBlock);

	obs_properties_add_color_alpha(props, "color", obs_module_text("Source.BpmDisplay.Color"));
	obs_properties_add_bool(props, "use_zone_colors", obs_module_text("Source.BpmDisplay.UseZoneColors"));
	obs_properties_add_float(props, "active_bpm", obs_module_text("Source.BpmDisplay.ActiveBpm"), 1.0, 300.0,
				 1.0);
	obs_properties_add_color_alpha(props, "resting_color", obs_module_text("Source.BpmDisplay.RestingColor"));
	obs_properties_add_color_alpha(props, "active_color", obs_module_text("Source.BpmDisplay.ActiveColor"));
	obs_properties_add_float(props, "high_bpm", obs_module_text("Source.BpmDisplay.HighBpm"), 1.0, 300.0,
				 1.0);
	obs_properties_add_color_alpha(props, "high_color", obs_module_text("Source.BpmDisplay.HighColor"));
	obs_properties_add_float(props, "peak_bpm", obs_module_text("Source.BpmDisplay.PeakBpm"), 1.0, 300.0,
				 1.0);
	obs_properties_add_color_alpha(props, "peak_color", obs_module_text("Source.BpmDisplay.PeakColor"));
	obs_properties_add_int(props, "width", obs_module_text("Source.BpmDisplay.Width"), 120, 1920, 1);
	obs_properties_add_int(props, "height", obs_module_text("Source.BpmDisplay.Height"), 80, 1080, 1);
	obs_properties_add_bool(props, "use_smoothed", obs_module_text("Source.BpmDisplay.UseSmoothed"));
	obs_property_t *glow_enabled =
		obs_properties_add_bool(props, "glow_enabled", obs_module_text("Source.BpmDisplay.GlowEnabled"));
	obs_property_set_modified_callback(glow_enabled, glow_enabled_changed);
	obs_properties_add_float_slider(props, "glow_softness", obs_module_text("Source.BpmDisplay.GlowSoftness"), 1.0,
					32.0, 0.5);
	obs_property_t *outline_enabled = obs_properties_add_bool(props, "outline_enabled",
								  obs_module_text("Source.BpmDisplay.OutlineEnabled"));
	obs_property_set_modified_callback(outline_enabled, outline_enabled_changed);
	obs_properties_add_color_alpha(props, "outline_color", obs_module_text("Source.BpmDisplay.OutlineColor"));
	obs_properties_add_int_slider(props, "outline_size", obs_module_text("Source.BpmDisplay.OutlineSize"), 1, 32,
				      1);
	obs_properties_add_bool(props, "show_when_disconnected",
				obs_module_text("Source.BpmDisplay.ShowWhenDisconnected"));
	return props;
}

void bpm_display_defaults(obs_data_t *settings)
{
	obs_data_t *font = obs_data_create();
	obs_data_set_default_string(font, "face", "Helvetica Neue");
	obs_data_set_default_string(font, "style", "Bold");
	obs_data_set_default_int(font, "size", 96);
	obs_data_set_default_int(font, "flags", OBS_FONT_BOLD);
	obs_data_set_default_obj(settings, "font", font);
	obs_data_release(font);

	obs_data_set_default_int(settings, "color", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "resting_color", 0xFF38D878);
	obs_data_set_default_int(settings, "active_color", 0xFF47A8FF);
	obs_data_set_default_int(settings, "high_color", 0xFF00D7FF);
	obs_data_set_default_int(settings, "peak_color", 0xFF3B3BFF);
	obs_data_set_default_int(settings, "render_mode", RenderModeSystemFont);
	obs_data_set_default_int(settings, "digit_style", DigitStyleDigital);
	obs_data_set_default_bool(settings, "use_zone_colors", false);
	obs_data_set_default_double(settings, "active_bpm", 100.0);
	obs_data_set_default_double(settings, "high_bpm", 130.0);
	obs_data_set_default_double(settings, "peak_bpm", 160.0);
	obs_data_set_default_int(settings, "width", 360);
	obs_data_set_default_int(settings, "height", 160);
	obs_data_set_default_bool(settings, "use_smoothed", true);
	obs_data_set_default_bool(settings, "glow_enabled", false);
	obs_data_set_default_double(settings, "glow_softness", 6.0);
	obs_data_set_default_bool(settings, "outline_enabled", false);
	obs_data_set_default_int(settings, "outline_color", 0xFF000000);
	obs_data_set_default_int(settings, "outline_size", 4);
	obs_data_set_default_bool(settings, "show_when_disconnected", true);
}

uint32_t bpm_display_width(void *data)
{
	return static_cast<BpmDisplaySource *>(data)->width;
}

uint32_t bpm_display_height(void *data)
{
	return static_cast<BpmDisplaySource *>(data)->height;
}

} // namespace

obs_source_info bpm_display_source_info = [] {
	obs_source_info info = {};
	info.id = "hyperate_bpm_display";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = bpm_display_get_name;
	info.create = bpm_display_create;
	info.destroy = bpm_display_destroy;
	info.update = bpm_display_update;
	info.get_defaults = bpm_display_defaults;
	info.get_properties = bpm_display_properties;
	info.get_width = bpm_display_width;
	info.get_height = bpm_display_height;
	info.video_render = bpm_display_render;
	return info;
}();
