#include <Halide.h>
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <iostream>

using namespace std;
using namespace Halide;


int main(int argc, char **argv) {

	ImageParam input_image(UInt(8), 2);
	Func input("input");

	float r_sigma = 4;
	int s_sigma = 5;
	Var x("x"), y("y"), z("z"), c("c");
	input(x, y) = cast<float>(input_image(x, y) / 255.0);

	// Add a boundary condition
	Func clamped("clamped");
	clamped(x, y) = input(clamp(x, 0, input_image.width() - 1),
		clamp(y, 0, input_image.height() - 1));

	// Construct the bilateral grid
	RDom r(0, s_sigma, 0, s_sigma);
	Expr val = clamped(x * s_sigma + r.x - s_sigma / 2, y * s_sigma + r.y - s_sigma / 2);
	val = clamp(val, 0.0f, 1.0f);
	Expr zi = cast<int>(val * (1.0f / r_sigma) + 0.5f);
	Func histogram("histogram");
	histogram(x, y, z, c) = 0.0f;
	histogram(x, y, zi, c) += select(c == 0, val, 1.0f);

	// Blur the grid using a five-tap filter
	Func blurx("blurx"), blury("blury"), blurz("blurz");
	blurz(x, y, z, c) = (histogram(x, y, z - 2, c) +
		histogram(x, y, z - 1, c) * 4 +
		histogram(x, y, z, c) * 6 +
		histogram(x, y, z + 1, c) * 4 +
		histogram(x, y, z + 2, c));
	blurx(x, y, z, c) = (blurz(x - 2, y, z, c) +
		blurz(x - 1, y, z, c) * 4 +
		blurz(x, y, z, c) * 6 +
		blurz(x + 1, y, z, c) * 4 +
		blurz(x + 2, y, z, c));
	blury(x, y, z, c) = (blurx(x, y - 2, z, c) +
		blurx(x, y - 1, z, c) * 4 +
		blurx(x, y, z, c) * 6 +
		blurx(x, y + 1, z, c) * 4 +
		blurx(x, y + 2, z, c));

	// Take trilinear samples to compute the output
	val = clamp(input(x, y), 0.0f, 1.0f);
	Expr zv = val * (1.0f / r_sigma);
	zi = cast<int>(zv);
	Expr zf = zv - zi;
	Expr xf = cast<float>(x % s_sigma) / s_sigma;
	Expr yf = cast<float>(y % s_sigma) / s_sigma;
	Expr xi = x / s_sigma;
	Expr yi = y / s_sigma;
	Func interpolated("interpolated");
	interpolated(x, y, c) =
		lerp(lerp(lerp(blury(xi, yi, zi, c), blury(xi + 1, yi, zi, c), xf),
		lerp(blury(xi, yi + 1, zi, c), blury(xi + 1, yi + 1, zi, c), xf), yf),
		lerp(lerp(blury(xi, yi, zi + 1, c), blury(xi + 1, yi, zi + 1, c), xf),
		lerp(blury(xi, yi + 1, zi + 1, c), blury(xi + 1, yi + 1, zi + 1, c), xf), yf), zf);

	// Normalize
	Func bilateral_grid("bilateral_grid");
	bilateral_grid(x, y) = interpolated(x, y, 0) / interpolated(x, y, 1);

	Target target = get_target_from_environment();
	if (target.has_gpu_feature()) {
		histogram.compute_root().reorder(c, z, x, y).gpu_tile(x, y, 8, 8);
		histogram.update().reorder(c, r.x, r.y, x, y).gpu_tile(x, y, 8, 8).unroll(c);
		blurx.compute_root().gpu_tile(x, y, z, 16, 16, 1);
		blury.compute_root().gpu_tile(x, y, z, 16, 16, 1);
		blurz.compute_root().gpu_tile(x, y, z, 8, 8, 4);
		bilateral_grid.compute_root().gpu_tile(x, y, s_sigma, s_sigma);
	}
	else {

		// CPU schedule
		histogram.compute_at(blurz, y);
		histogram.update().reorder(c, r.x, r.y, x, y).unroll(c);
		blurz.compute_root().reorder(c, z, x, y).parallel(y).vectorize(x, 4).unroll(c);
		blurx.compute_root().reorder(c, x, y, z).parallel(z).vectorize(x, 4).unroll(c);
		blury.compute_root().reorder(c, x, y, z).parallel(z).vectorize(x, 4).unroll(c);
		bilateral_grid.compute_root().parallel(y).vectorize(x, 4);
	}

	Func output("output");
	output(x, y) = cast<uint8_t>(255.0 * bilateral_grid(x, y));

	output.compile_to_file("halide_bilateral_gen", input_image);

	return 0;

}
