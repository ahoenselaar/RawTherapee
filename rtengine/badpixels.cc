/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2019 Gabor Horvath <hgabor@rawtherapee.com>
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "array2D.h"
#include "median.h"
#include "pixelsmap.h"
#include "rawimage.h"
#include "rawimagesource.h"

namespace
{
unsigned fc(const unsigned int cfa[2][2], int r, int c) {
    return cfa[r & 1][c & 1];
}
}

namespace rtengine
{

/* interpolateBadPixelsBayer: correct raw pixels looking at the bitmap
 * takes into consideration if there are multiple bad pixels in the neighborhood
 */
int RawImageSource::interpolateBadPixelsBayer(const PixelsMap &bitmapBads, array2D<float> &rawData)
{
    const unsigned int cfarray[2][2] = {{FC(0,0), FC(0,1)}, {FC(1,0), FC(1,1)}};
    constexpr float eps = 1.f;
    int counter = 0;

#ifdef _OPENMP
    #pragma omp parallel for reduction(+:counter) schedule(dynamic,16)
#endif

    for (int row = 2; row < H - 2; ++row) {
        for (int col = 2; col < W - 2; ++col) {
            const int sk = bitmapBads.skipIfZero(col, row); //optimization for a stripe all zero

            if (sk) {
                col += sk - 1; //-1 is because of col++ in cycle
                continue;
            }

            if (!bitmapBads.get(col, row)) {
                continue;
            }

            float wtdsum = 0.f, norm = 0.f;

            // diagonal interpolation
            if (fc(cfarray, row, col) == 1) {
                // green channel. We can use closer pixels than for red or blue channel. Distance to center pixel is sqrt(2) => weighting is 0.70710678
                // For green channel following pixels will be used for interpolation. Pixel to be interpolated is in center.
                // 1 means that pixel is used in this step, if itself and his counterpart are not marked bad
                // 0 0 0 0 0
                // 0 1 0 1 0
                // 0 0 0 0 0
                // 0 1 0 1 0
                // 0 0 0 0 0
                for (int dx = -1; dx <= 1; dx += 2) {
                    if (bitmapBads.get(col + dx, row - 1) || bitmapBads.get(col - dx, row + 1)) {
                        continue;
                    }

                    const float dirwt = 0.70710678f / (fabsf(rawData[row - 1][col + dx] - rawData[row + 1][col - dx]) + eps);
                    wtdsum += dirwt * (rawData[row - 1][col + dx] + rawData[row + 1][col - dx]);
                    norm += dirwt;
                }
            } else {
                // red and blue channel. Distance to center pixel is sqrt(8) => weighting is 0.35355339
                // For red and blue channel following pixels will be used for interpolation. Pixel to be interpolated is in center.
                // 1 means that pixel is used in this step, if itself and his counterpart are not marked bad
                // 1 0 0 0 1
                // 0 0 0 0 0
                // 0 0 0 0 0
                // 0 0 0 0 0
                // 1 0 0 0 1
                for (int dx = -2; dx <= 2; dx += 4) {
                    if (bitmapBads.get(col + dx, row - 2) || bitmapBads.get(col - dx, row + 2)) {
                        continue;
                    }

                    const float dirwt = 0.35355339f / (fabsf(rawData[row - 2][col + dx] - rawData[row + 2][col - dx]) + eps);
                    wtdsum += dirwt * (rawData[row - 2][col + dx] + rawData[row + 2][col - dx]);
                    norm += dirwt;
                }
            }

            // channel independent. Distance to center pixel is 2 => weighting is 0.5
            // Additionally for all channel following pixels will be used for interpolation. Pixel to be interpolated is in center.
            // 1 means that pixel is used in this step, if itself and his counterpart are not marked bad
            // 0 0 1 0 0
            // 0 0 0 0 0
            // 1 0 0 0 1
            // 0 0 0 0 0
            // 0 0 1 0 0

            // horizontal interpolation
            if (!(bitmapBads.get(col - 2, row) || bitmapBads.get(col + 2, row))) {
                const float dirwt = 0.5f / (fabsf(rawData[row][col - 2] - rawData[row][col + 2]) + eps);
                wtdsum += dirwt * (rawData[row][col - 2] + rawData[row][col + 2]);
                norm += dirwt;
            }

            // vertical interpolation
            if (!(bitmapBads.get(col, row - 2) || bitmapBads.get(col, row + 2))) {
                const float dirwt = 0.5f / (fabsf(rawData[row - 2][col] - rawData[row + 2][col]) + eps);
                wtdsum += dirwt * (rawData[row - 2][col] + rawData[row + 2][col]);
                norm += dirwt;
            }

            if (LIKELY(norm > 0.f)) { // This means, we found at least one pair of valid pixels in the steps above, likelihood of this case is about 99.999%
                rawData[row][col] = wtdsum / (2.f * norm); //gradient weighted average, Factor of 2.f is an optimization to avoid multiplications in former steps
                counter++;
            } else { //backup plan -- simple average. Same method for all channels. We could improve this, but it's really unlikely that this case happens
                int tot = 0;
                float sum = 0.f;

                for (int dy = -2; dy <= 2; dy += 2) {
                    for (int dx = -2; dx <= 2; dx += 2) {
                        if (bitmapBads.get(col + dx, row + dy)) {
                            continue;
                        }

                        sum += rawData[row + dy][col + dx];
                        tot++;
                    }
                }

                if (tot > 0) {
                    rawData[row][col] = sum / tot;
                    counter ++;
                }
            }
        }
    }

    return counter; // Number of interpolated pixels.
}

/* interpolateBadPixelsNcolors: correct raw pixels looking at the bitmap
 * takes into consideration if there are multiple bad pixels in the neighborhood
 */
int RawImageSource::interpolateBadPixelsNColours(const PixelsMap &bitmapBads, const int colors)
{
    constexpr float eps = 1.f;
    int counter = 0;

#ifdef _OPENMP
    #pragma omp parallel for reduction(+:counter) schedule(dynamic,16)
#endif

    for (int row = 2; row < H - 2; ++row) {
        for (int col = 2; col < W - 2; ++col) {
            const int sk = bitmapBads.skipIfZero(col, row); //optimization for a stripe all zero

            if (sk) {
                col += sk - 1; //-1 is because of col++ in cycle
                continue;
            }

            if (!bitmapBads.get(col, row)) {
                continue;
            }

            float wtdsum[colors];
            float norm[colors];
                for (int c = 0; c < colors; ++c) {
                    wtdsum[c] = norm[c] = 0.f;
                }

            // diagonal interpolation
            for (int dx = -1; dx <= 1; dx += 2) {
                if (bitmapBads.get(col + dx, row - 1) || bitmapBads.get(col - dx, row + 1)) {
                    continue;
                }

                for (int c = 0; c < colors; ++c) {
                    const float dirwt = 0.70710678f / (fabsf(rawData[row - 1][(col + dx) * colors + c] - rawData[row + 1][(col - dx) * colors + c]) + eps);
                    wtdsum[c] += dirwt * (rawData[row - 1][(col + dx) * colors + c] + rawData[row + 1][(col - dx) * colors + c]);
                    norm[c] += dirwt;
                }
            }

            // horizontal interpolation
            if (!(bitmapBads.get(col - 1, row) || bitmapBads.get(col + 1, row))) {
                for (int c = 0; c < colors; ++c) {
                    const float dirwt = 1.f / (fabsf(rawData[row][(col - 1) * colors + c] - rawData[row][(col + 1) * colors + c]) + eps);
                    wtdsum[c] += dirwt * (rawData[row][(col - 1) * colors + c] + rawData[row][(col + 1) * colors + c]);
                    norm[c] += dirwt;
                }
            }

            // vertical interpolation
            if (!(bitmapBads.get(col, row - 1) || bitmapBads.get(col, row + 1))) {
                for (int c = 0; c < colors; ++c) {
                    const float dirwt = 1.f / (fabsf(rawData[row - 1][col * colors + c] - rawData[row + 1][col * colors + c]) + eps);
                    wtdsum[c] += dirwt * (rawData[row - 1][col * colors + c] + rawData[row + 1][col * colors + c]);
                    norm[c] += dirwt;
                }
            }

            if (LIKELY(norm[0] > 0.f)) { // This means, we found at least one pair of valid pixels in the steps above, likelihood of this case is about 99.999%
                for (int c = 0; c < colors; ++c) {
                    rawData[row][col * colors + c] = wtdsum[c] / (2.f * norm[c]); //gradient weighted average, Factor of 2.f is an optimization to avoid multiplications in former steps
                }

                counter++;
            } else { //backup plan -- simple average. Same method for all channels. We could improve this, but it's really unlikely that this case happens
                int tot = 0;
                float sum[colors];
                for (int c = 0; c < colors; ++c) {
                    sum[c] = 0.f;
                }

                for (int dy = -2; dy <= 2; dy += 2) {
                    for (int dx = -2; dx <= 2; dx += 2) {
                        if (bitmapBads.get(col + dx, row + dy)) {
                            continue;
                        }

                        for (int c = 0; c < colors; ++c) {
                            sum[c] += rawData[row + dy][(col + dx) * colors + c];
                        }

                        tot++;
                    }
                }

                if (tot > 0) {
                    for (int c = 0; c < colors; ++c) {
                        rawData[row][col * colors + c] = sum[c] / tot;
                    }

                    counter ++;
                }
            }
        }
    }

    return counter; // Number of interpolated pixels.
}

/* interpolateBadPixelsXtrans: correct raw pixels looking at the bitmap
 * takes into consideration if there are multiple bad pixels in the neighborhood
 */
int RawImageSource::interpolateBadPixelsXtrans(const PixelsMap &bitmapBads)
{
    constexpr float eps = 1.f;
    int counter = 0;

#ifdef _OPENMP
    #pragma omp parallel for reduction(+:counter) schedule(dynamic,16)
#endif

    for (int row = 2; row < H - 2; ++row) {
        for (int col = 2; col < W - 2; ++col) {
            const int skip = bitmapBads.skipIfZero(col, row); //optimization for a stripe all zero

            if (skip) {
                col += skip - 1; //-1 is because of col++ in cycle
                continue;
            }

            if (!bitmapBads.get(col, row)) {
                continue;
            }

            float wtdsum = 0.f, norm = 0.f;
            const unsigned int pixelColor = ri->XTRANSFC(row, col);

            if (pixelColor == 1) {
                // green channel. A green pixel can either be a solitary green pixel or a member of a 2x2 square of green pixels
                if (ri->XTRANSFC(row, col - 1) == ri->XTRANSFC(row, col + 1)) {
                    // If left and right neighbor have same color, then this is a solitary green pixel
                    // For these the following pixels will be used for interpolation. Pixel to be interpolated is in center and marked with a P.
                    // Pairs of pixels used in this step are numbered. A pair will be used if none of the pixels of the pair is marked bad
                    // 0 means, the pixel has a different color and will not be used
                    // 0 1 0 2 0
                    // 3 5 0 6 4
                    // 0 0 P 0 0
                    // 4 6 0 5 3
                    // 0 2 0 1 0
                    for (int dx = -1; dx <= 1; dx += 2) { // pixels marked 5 or 6 in above example. Distance to P is sqrt(2) => weighting is 0.70710678f
                        if (bitmapBads.get(col + dx, row - 1) || bitmapBads.get(col - dx, row + 1)) {
                            continue;
                        }

                        const float dirwt = 0.70710678f / (fabsf(rawData[row - 1][col + dx] - rawData[row + 1][col - dx]) + eps);
                        wtdsum += dirwt * (rawData[row - 1][col + dx] + rawData[row + 1][col - dx]);
                        norm += dirwt;
                    }

                    for (int dx = -1; dx <= 1; dx += 2) { // pixels marked 1 or 2 on above example. Distance to P is sqrt(5) => weighting is 0.44721359f
                        if (bitmapBads.get(col + dx, row - 2) || bitmapBads.get(col - dx, row + 2)) {
                            continue;
                        }

                        const float dirwt = 0.44721359f / (fabsf(rawData[row - 2][col + dx] - rawData[row + 2][col - dx]) + eps);
                        wtdsum += dirwt * (rawData[row - 2][col + dx] + rawData[row + 2][col - dx]);
                        norm += dirwt;
                    }

                    for (int dx = -2; dx <= 2; dx += 4) { // pixels marked 3 or 4 on above example. Distance to P is sqrt(5) => weighting is 0.44721359f
                        if (bitmapBads.get(col + dx, row - 1) || bitmapBads.get(col - dx, row + 1)) {
                            continue;
                        }

                        const float dirwt = 0.44721359f / (fabsf(rawData[row - 1][col + dx] - rawData[row + 1][col - dx]) + eps);
                        wtdsum += dirwt * (rawData[row - 1][col + dx] + rawData[row + 1][col - dx]);
                        norm += dirwt;
                    }
                } else {
                    // this is a member of a 2x2 square of green pixels
                    // For these the following pixels will be used for interpolation. Pixel to be interpolated is at position P in the example.
                    // Pairs of pixels used in this step are numbered. A pair will be used if none of the pixels of the pair is marked bad
                    // 0 means, the pixel has a different color and will not be used
                    // 1 0 0 3
                    // 0 P 2 0
                    // 0 2 1 0
                    // 3 0 0 0

                    // pixels marked 1 in above example. Distance to P is sqrt(2) => weighting is 0.70710678f
                    const int offset1 = ri->XTRANSFC(row - 1, col - 1) == ri->XTRANSFC(row + 1, col + 1) ? 1 : -1;

                    if (!(bitmapBads.get(col - offset1, row - 1) || bitmapBads.get(col + offset1, row + 1))) {
                        const float dirwt = 0.70710678f / (fabsf(rawData[row - 1][col - offset1] - rawData[row + 1][col + offset1]) + eps);
                        wtdsum += dirwt * (rawData[row - 1][col - offset1] + rawData[row + 1][col + offset1]);
                        norm += dirwt;
                    }

                    // pixels marked 2 in above example. Distance to P is 1 => weighting is 1.f
                    int offsety = ri->XTRANSFC(row - 1, col) != 1 ? 1 : -1;
                    int offsetx = offset1 * offsety;

                    if (!(bitmapBads.get(col + offsetx, row) || bitmapBads.get(col, row + offsety))) {
                        const float dirwt = 1.f / (fabsf(rawData[row][col + offsetx] - rawData[row + offsety][col]) + eps);
                        wtdsum += dirwt * (rawData[row][col + offsetx] + rawData[row + offsety][col]);
                        norm += dirwt;
                    }

                    const int offsety2 = -offsety;
                    const int offsetx2 = -offsetx;
                    offsetx *= 2;
                    offsety *= 2;

                    // pixels marked 3 in above example. Distance to P is sqrt(5) => weighting is 0.44721359f
                    if (!(bitmapBads.get(col + offsetx, row + offsety2) || bitmapBads.get(col + offsetx2, row + offsety))) {
                        const float dirwt = 0.44721359f / (fabsf(rawData[row + offsety2][col + offsetx] - rawData[row + offsety][col + offsetx2]) + eps);
                        wtdsum += dirwt * (rawData[row + offsety2][col + offsetx] + rawData[row + offsety][col + offsetx2]);
                        norm += dirwt;
                    }
                }
            } else {
                // red and blue channel.
                // Each red or blue pixel has exactly one neighbor of same color in distance 2 and four neighbors of same color which can be reached by a move of a knight in chess.
                // For the distance 2 pixel (marked with an X) we generate a virtual counterpart (marked with a V)
                // For red and blue channel following pixels will be used for interpolation. Pixel to be interpolated is in center and marked with a P.
                // Pairs of pixels used in this step are numbered except for distance 2 pixels which are marked X and V. A pair will be used if none of the pixels of the pair is marked bad
                // 0 1 0 0 0    0 0 X 0 0   remaining cases are symmetric
                // 0 0 0 0 2    1 0 0 0 2
                // X 0 P 0 V    0 0 P 0 0
                // 0 0 0 0 1    0 0 0 0 0
                // 0 2 0 0 0    0 2 V 1 0

                // Find two knight moves landing on a pixel of same color as the pixel to be interpolated.
                // If we look at first and last row of 5x5 square, we will find exactly two knight pixels.
                // Additionally we know that the column of this pixel has 1 or -1 horizontal distance to the center pixel
                // When we find a knight pixel, we get its counterpart, which has distance (+-3,+-3), where the signs of distance depend on the corner of the found knight pixel.
                // These pixels are marked 1 or 2 in above examples. Distance to P is sqrt(5) => weighting is 0.44721359f
                // The following loop simply scans the four possible places. To keep things simple, it does not stop after finding two knight pixels, because it will not find more than two
                for (int d1 = -2, offsety = 3; d1 <= 2; d1 += 4, offsety -= 6) {
                    for (int d2 = -1, offsetx = 3; d2 < 1; d2 += 2, offsetx -= 6) {
                        if (ri->XTRANSFC(row + d1, col + d2) == pixelColor) {
                            if (!(bitmapBads.get(col + d2, row + d1) || bitmapBads.get(col + d2 + offsetx, row + d1 + offsety))) {
                                const float dirwt = 0.44721359f / (fabsf(rawData[row + d1][col + d2] - rawData[row + d1 + offsety][col + d2 + offsetx]) + eps);
                                wtdsum += dirwt * (rawData[row + d1][col + d2] + rawData[row + d1 + offsety][col + d2 + offsetx]);
                                norm += dirwt;
                            }
                        }
                    }
                }

                // now scan for the pixel of same color in distance 2 in each direction (marked with an X in above examples).
                bool distance2PixelFound = false;
                int dx, dy;

                // check horizontal
                for (dx = -2, dy = 0; dx <= 2; dx += 4) {
                    if (ri->XTRANSFC(row, col + dx) == pixelColor) {
                        distance2PixelFound = true;
                        break;
                    }
                }

                if (!distance2PixelFound) {
                    // no distance 2 pixel on horizontal, check vertical
                    for (dx = 0, dy = -2; dy <= 2; dy += 4) {
                        if (ri->XTRANSFC(row + dy, col) == pixelColor) {
                            distance2PixelFound = true;
                            break;
                        }
                    }
                }

                // calculate the value of its virtual counterpart (marked with a V in above examples)
                float virtualPixel;

                if (dy == 0) {
                    virtualPixel = 0.5f * (rawData[row - 1][col - dx] + rawData[row + 1][col - dx]);
                } else {
                    virtualPixel = 0.5f * (rawData[row - dy][col - 1] + rawData[row - dy][col + 1]);
                }

                // and weight as usual. Distance to P is 2 => weighting is 0.5f
                const float dirwt = 0.5f / (fabsf(virtualPixel - rawData[row + dy][col + dx]) + eps);
                wtdsum += dirwt * (virtualPixel + rawData[row + dy][col + dx]);
                norm += dirwt;
            }

            if (LIKELY(norm > 0.f)) { // This means, we found at least one pair of valid pixels in the steps above, likelihood of this case is about 99.999%
                rawData[row][col] = wtdsum / (2.f * norm); //gradient weighted average, Factor of 2.f is an optimization to avoid multiplications in former steps
                counter++;
            }
        }
    }

    return counter; // Number of interpolated pixels.
}

/*  Search for hot or dead pixels in the image and update the map
 *  For each pixel compare its value to the average of similar color surrounding
 *  (Taken from Emil Martinec idea)
 *  (Optimized by Ingo Weyrich 2013 and 2015)
 */
int RawImageSource::findHotDeadPixels(PixelsMap &bpMap, const float thresh, const bool findHotPixels, const bool findDeadPixels) const
{
    const float varthresh = (20.0 * (thresh / 100.0) + 1.0) / 24.f;

    // allocate temporary buffer
    float* cfablur = new float[H * W];

    // counter for dead or hot pixels
    int counter = 0;

#ifdef _OPENMP
    #pragma omp parallel
#endif
    {
#ifdef _OPENMP
        #pragma omp for schedule(dynamic,16) nowait
#endif

        for (int i = 2; i < H - 2; i++) {
            for (int j = 2; j < W - 2; j++) {
                const float temp = median(rawData[i - 2][j - 2], rawData[i - 2][j], rawData[i - 2][j + 2],
                                          rawData[i][j - 2], rawData[i][j], rawData[i][j + 2],
                                          rawData[i + 2][j - 2], rawData[i + 2][j], rawData[i + 2][j + 2]);
                cfablur[i * W + j] = rawData[i][j] - temp;
            }
        }

        // process borders. Former version calculated the median using mirrored border which does not make sense because the original pixel loses weight
        // Setting the difference between pixel and median for border pixels to zero should do the job not worse then former version
#ifdef _OPENMP
        #pragma omp single
#endif
        {
            for (int i = 0; i < 2; ++i) {
                for (int j = 0; j < W; ++j) {
                    cfablur[i * W + j] = 0.f;
                }
            }

            for (int i = 2; i < H - 2; ++i) {
                for (int j = 0; j < 2; ++j) {
                    cfablur[i * W + j] = 0.f;
                }

                for (int j = W - 2; j < W; ++j) {
                    cfablur[i * W + j] = 0.f;
                }
            }

            for (int i = H - 2; i < H; ++i) {
                for (int j = 0; j < W; ++j) {
                    cfablur[i * W + j] = 0.f;
                }
            }
        }

#ifdef _OPENMP
        #pragma omp barrier // barrier because of nowait clause above

        #pragma omp for reduction(+:counter) schedule(dynamic,16)
#endif

        //cfa pixel heat/death evaluation
        for (int rr = 2; rr < H - 2; ++rr) {
            for (int cc = 2, rrmWpcc = rr * W + 2; cc < W - 2; ++cc, ++rrmWpcc) {
                //evaluate pixel for heat/death
                float pixdev = cfablur[rrmWpcc];

                if (pixdev == 0.f) {
                    continue;
                }

                if ((!findDeadPixels) && pixdev < 0) {
                    continue;
                }

                if ((!findHotPixels) && pixdev > 0) {
                    continue;
                }

                pixdev = fabsf(pixdev);
                float hfnbrave = -pixdev;

#ifdef __SSE2__
                // sum up 5*4 = 20 values using SSE
                // 10 fabs function calls and 10 float additions with SSE
                vfloat sum = vabsf(LVFU(cfablur[(rr - 2) * W + cc - 2])) + vabsf(LVFU(cfablur[(rr - 1) * W + cc - 2]));
                sum += vabsf(LVFU(cfablur[(rr) * W + cc - 2]));
                sum += vabsf(LVFU(cfablur[(rr + 1) * W + cc - 2]));
                sum += vabsf(LVFU(cfablur[(rr + 2) * W + cc - 2]));
                // horizontally add the values and add the result to hfnbrave
                hfnbrave += vhadd(sum);

                // add remaining 5 values of last column
                for (int mm = rr - 2; mm <= rr + 2; ++mm) {
                    hfnbrave += fabsf(cfablur[mm * W + cc + 2]);
                }

#else

                //  25 fabs function calls and 25 float additions without SSE
                for (int mm = rr - 2; mm <= rr + 2; ++mm) {
                    for (int nn = cc - 2; nn <= cc + 2; ++nn) {
                        hfnbrave += fabsf(cfablur[mm * W + nn]);
                    }
                }

#endif

                if (pixdev > varthresh * hfnbrave) {
                    // mark the pixel as "bad"
                    bpMap.set(cc, rr);
                    counter++;
                }
            }//end of pixel evaluation
        }
    }//end of parallel processing
    delete [] cfablur;
    return counter;
}

int RawImageSource::findZeroPixels(PixelsMap &bpMap) const
{
    int counter = 0;

#ifdef _OPENMP
    #pragma omp parallel for reduction(+:counter) schedule(dynamic,16)
#endif

    for (int i = 0; i < H; ++i) {
        for (int j = 0; j < W; ++j) {
            if (ri->data[i][j] == 0.f) {
                bpMap.set(j, i);
                counter++;
            }
        }
    }
    return counter;
}


}
