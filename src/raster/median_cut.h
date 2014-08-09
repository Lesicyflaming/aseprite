/* Aseprite
 * Copyright (C) 2001-2013  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef RASTER_MEDIAN_CUT_H_INCLUDED
#define RASTER_MEDIAN_CUT_H_INCLUDED
#pragma once

#include <list>
#include <queue>

namespace raster {
namespace quantization {

  template<class Histogram>
  class Box {

    // These classes are used as parameters for some Box's generic
    // member functions, so we can access to a different axis using
    // the same generic function (i=Red channel in RAxisGetter, etc.).
    struct RAxisGetter { static size_t at(const Histogram& h, int i, int j, int k) { return h.at(i, j, k); } };
    struct GAxisGetter { static size_t at(const Histogram& h, int i, int j, int k) { return h.at(j, i, k); } };
    struct BAxisGetter { static size_t at(const Histogram& h, int i, int j, int k) { return h.at(j, k, i); } };

    // These classes are used as template parameter to split a Box
    // along an axis (see splitAlongAxis)
    struct RAxisSplitter {
      static Box box1(const Box& box, int r) { return Box(box.r1, box.g1, box.b1, r,      box.g2, box.b2); }
      static Box box2(const Box& box, int r) { return Box(r,      box.g1, box.b1, box.r2, box.g2, box.b2); }
    };
    struct GAxisSplitter {
      static Box box1(const Box& box, int g) { return Box(box.r1, box.g1, box.b1, box.r2, g,      box.b2); }
      static Box box2(const Box& box, int g) { return Box(box.r1, g,      box.b1, box.r2, box.g2, box.b2); }
    };
    struct BAxisSplitter {
      static Box box1(const Box& box, int b) { return Box(box.r1, box.g1, box.b1, box.r2, box.g2, b     ); }
      static Box box2(const Box& box, int b) { return Box(box.r1, box.g1, b,      box.r2, box.g2, box.b2); }
    };

  public:
    Box(int r1, int g1, int b1,
        int r2, int g2, int b2)
      : r1(r1), g1(g1), b1(b1)
      , r2(r2), g2(g2), b2(b2)
      , volume(calculateVolume())
      , points(0) { }

    // Shrinks each plane of the box to a position where there are
    // points in the histogram.
    void shrink(const Histogram& histogram)
    {
      axisShrink<RAxisGetter>(histogram, r1, r2, g1, g2, b1, b2);
      axisShrink<GAxisGetter>(histogram, g1, g2, r1, r2, b1, b2);
      axisShrink<BAxisGetter>(histogram, b1, b2, r1, r2, g1, g2);

      // Calculate number of points inside the box (this is done by
      // first time here, because the Box ctor didn't calculate it).
      points = countPoints(histogram);

      // Recalculate the volume (used in operator<).
      volume = calculateVolume();
    }

    bool split(const Histogram& histogram, std::priority_queue<Box>& boxes) const
    {
      // Split along the largest dimension of the box.
      if ((r2-r1) >= (g2-g1) && (r2-r1) >= (b2-b1)) {
        return splitAlongAxis<RAxisGetter, RAxisSplitter>(histogram, boxes, r1, r2, g1, g2, b1, b2);
      }
      else if ((g2-g1) >= (r2-r1) && (g2-g1) >= (b2-b1)) {
        return splitAlongAxis<GAxisGetter, GAxisSplitter>(histogram, boxes, g1, g2, r1, r2, b1, b2);
      }
      else {
        return splitAlongAxis<BAxisGetter, BAxisSplitter>(histogram, boxes, b1, b2, r1, r2, g1, g2);
      }
    }

    // Returns the color enclosed by the box calculating the mean of
    // all histogram's points inside the box.
    uint32_t meanColor(const Histogram& histogram) const
    {
      size_t r = 0, g = 0, b = 0;
      size_t count = 0;
      int i, j, k;

      for (i=r1; i<=r2; ++i)
        for (j=g1; j<=g2; ++j)
          for (k=b1; k<=b2; ++k) {
            int c = histogram.at(i, j, k);
            r += c * i;
            g += c * j;
            b += c * k;
            count += c;
          }

      // No colors in the box? This should not be possible.
      ASSERT(count > 0 && "Box without histogram points, you must fill the histogram before using this function.");
      if (count == 0)
        return rgba(0, 0, 0, 255);

      // Returns the mean.
      return rgba((255 * r / (Histogram::RElements-1)) / count,
                  (255 * g / (Histogram::GElements-1)) / count,
                  (255 * b / (Histogram::BElements-1)) / count, 255);
    }

    // The boxes will be sort in the priority_queue by volume.
    bool operator<(const Box& other) const
    {
      return volume < other.volume;
    }

  private:

    // Calculates the volume from the current box's dimensions. The
    // value returned by this function is cached in the "volume"
    // variable member of Box class to avoid multiplying several
    // times.
    int calculateVolume() const
    {
      return (r2-r1+1) * (g2-g1+1) * (b2-b1+1);
    }

    // Returns the number of histogram's points inside the box bounds.
    size_t countPoints(const Histogram& histogram) const
    {
      size_t count = 0;
      int i, j, k;

      for (i=r1; i<=r2; ++i)
        for (j=g1; j<=g2; ++j)
          for (k=b1; k<=b2; ++k)
            count += histogram.at(i, j, k);

      return count;
    }

    // Reduces the specified side of the box (i1/i2) along the
    // specified axis (if AxisGetter is RAxisGetter, then i1=r1,
    // i2=r2; if AxisGetter is GAxisGetter, then i1=g1, i2=g2).
    template<class AxisGetter>
    static void axisShrink(const Histogram& histogram,
                           int& i1, int& i2,
                           const int& j1, const int& j2,
                           const int& k1, const int& k2)
    {
      int j, k;

      // Shrink i1.
      for (; i1<i2; ++i1) {
        for (j=j1; j<=j2; ++j) {
          for (k=k1; k<=k2; ++k) {
            if (AxisGetter::at(histogram, i1, j, k) > 0)
              goto doneA;
          }
        }
      }

    doneA:;

      for (; i2>i1; --i2) {
        for (j=j1; j<=j2; ++j) {
          for (k=k1; k<=k2; ++k) {
            if (AxisGetter::at(histogram, i2, j, k) > 0)
              goto doneB;
          }
        }
      }

    doneB:;
    }

    // Splits the box in two sub-boxes (if it's possible) along the
    // specified axis by AxisGetter template parameter and "i1/i2"
    // arguments. Returns true if the split was done and the "boxes"
    // queue contains the new two sub-boxes resulting from the split
    // operation.
    template<class AxisGetter, class AxisSplitter>
    bool splitAlongAxis(const Histogram& histogram,
                        std::priority_queue<Box>& boxes,
                        const int& i1, const int& i2,
                        const int& j1, const int& j2,
                        const int& k1, const int& k2) const
    {
      // These two variables will be used to count how many points are
      // in each side of the box if we split it in "i" position.
      size_t totalPoints1 = 0;
      size_t totalPoints2 = this->points;
      int i, j, k;

      // We will try to split the box along the "i" axis. Imagine a
      // plane which its normal vector is "i" axis, so we will try to
      // move this plane from "i1" to "i2" to find the median, where
      // the number of points in both sides of the plane are
      // approximated the same.
      for (i=i1; i<=i2; ++i) {
        size_t planePoints = 0;

        // We count all points in "i" plane.
        for (j=j1; j<=j2; ++j)
          for (k=k1; k<=k2; ++k)
            planePoints += AxisGetter::at(histogram, i, j, k);

        // As we move the plane to split through "i" axis One side is getting more points,
        totalPoints1 += planePoints;
        totalPoints2 -= planePoints;

        if (totalPoints1 > totalPoints2) {
          if (totalPoints2 > 0) {
            Box box1(AxisSplitter::box1(*this, i));
            Box box2(AxisSplitter::box2(*this, i+1));
            box1.points = totalPoints1;
            box2.points = totalPoints2;
            boxes.push(box1);
            boxes.push(box2);
            return true;
          }
          else if (totalPoints1-planePoints > 0) {
            Box box1(AxisSplitter::box1(*this, i-1));
            Box box2(AxisSplitter::box2(*this, i));
            box1.points = totalPoints1-planePoints;
            box2.points = totalPoints2+planePoints;
            boxes.push(box1);
            boxes.push(box2);
            return true;
          }
          else
            return false;
        }
      }
      return false;
    }

    int r1, g1, b1;             // Min point (closest to origin)
    int r2, g2, b2;             // Max point
    size_t points;              // Number of points in the space which enclose this box
    int volume;
  }; // end of class Box

  // Median Cut Algorithm as described in P. Heckbert, "Color image
  // quantization for frame buffer display,", Computer Graphics,
  // 16(3), pp. 297-307 (1982)
  template<class Histogram>
  void median_cut(const Histogram& histogram, size_t maxBoxes, std::vector<uint32_t>& result)
  {
    // We need a priority queue to split bigger boxes first (see Box::operator<).
    std::priority_queue<Box<Histogram> > boxes;

    // First we start with one big box containing all histogram's samples.
    boxes.push(Box<Histogram>(0, 0, 0,
                              Histogram::RElements-1,
                              Histogram::GElements-1,
                              Histogram::BElements-1));

    // Then we split each box until we reach the maximum specified by
    // the user (maxBoxes) or until there aren't more boxes to split.
    while (!boxes.empty() && boxes.size() < maxBoxes) {
      // Get and remove the first (bigger) box to process from "boxes" queue.
      Box<Histogram> box(boxes.top());
      boxes.pop();

      // Shrink the box to the minimum, to enclose the same points in
      // the histogram.
      box.shrink(histogram);

      // Try to split the box along the largest axis.
      if (!box.split(histogram, boxes)) {
        // If we were not able to split the box (maybe because it is
        // too small or there are not enough points to split it), then
        // we add the box's color to the "result" vector directly (the
        // box is not in the queue anymore).
        if (result.size() < maxBoxes)
          result.push_back(box.meanColor(histogram));
        else
          return;
      }
    }

    // When we reach the maximum number of boxes, we convert each box
    // to a color for the "result" vector.
    while (!boxes.empty() && result.size() < maxBoxes) {
      const Box<Histogram>& box(boxes.top());
      result.push_back(box.meanColor(histogram));
      boxes.pop();
    }
  }

} // namespace quantization
} // namespace raster

#endif
