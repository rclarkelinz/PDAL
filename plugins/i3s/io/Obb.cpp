/******************************************************************************
* Copyright (c) 2020, Hobu Inc. (info@hobu.co)
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following
* conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in
*       the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of Hobu, Inc. or Flaxen Geo Consulting nor the
*       names of its contributors may be used to endorse or promote
*       products derived from this software without specific prior
*       written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
* OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
* OF SUCH DAMAGE.
****************************************************************************/

#include <array>

#include <nlohmann/json.hpp>

#include <pdal/util/Bounds.hpp>
#include <pdal/private/SrsTransform.hpp>

#include "Obb.hpp"

namespace
{

Eigen::Vector3d rotate(const Eigen::Vector3d& v, Eigen::Quaterniond rot)
{
    rot.normalize();
    Eigen::Quaterniond p;
    p.w() = 0;
    p.vec() = v;
    p = rot * p * rot.inverse();
    return p.vec();
}

} //unnamed namespace

namespace pdal
{
namespace i3s
{

Obb::Obb(const NL::json& spec)
{
    parse(spec);
}

void Obb::verifyArray(const NL::json& spec, const std::string& name, size_t cnt)
{
    if (spec.count(name) != 1)
    {
        throw EsriError("Invalid OBB - missing '" + name + "' entry.");
    }
    NL::json arr = spec[name];
    if (!arr.is_array())
        throw EsriError("Invalid OBB - '" + name + "' is not an array.");
    if (arr.size() != cnt)
        throw EsriError("Invalid OBB - '" + name + "' does not specify "
                "three values.");
    for (size_t i = 0; i < cnt; ++i)
    {
        NL::json o = arr[i];
        if (!o.is_number())
            throw EsriError("Invalid OBB - '" + name +
                "[" + std::to_string(i) + "]' " "is not numeric.");
    }
}

void Obb::parse(NL::json spec)
{
    verifyArray(spec, "center", 3);
    verifyArray(spec, "halfSize", 3);
    verifyArray(spec, "quaternion", 4);

    double x = spec["center"][0].get<double>();
    double y = spec["center"][1].get<double>();
    double z = spec["center"][2].get<double>();
    m_p = { x, y, z };

    m_hx = spec["halfSize"][0].get<double>();
    m_hy = spec["halfSize"][1].get<double>();
    m_hz = spec["halfSize"][2].get<double>();

    double qx = spec["quaternion"][0].get<double>();
    double qy = spec["quaternion"][1].get<double>();
    double qz = spec["quaternion"][2].get<double>();
    double qw = spec["quaternion"][3].get<double>();

    m_quat = Eigen::Quaterniond(qw, qx, qy, qz);

    spec.erase("center");
    spec.erase("halfSize");
    spec.erase("quaternion");
    if (spec.size())
    {
        throw EsriError("Invalid OBB: found invalid key '" +
            spec.begin().key() + "'.");
    }
}

void Obb::transform(const SrsTransform& xform)
{
    xform.transform(m_p.x(), m_p.y(), m_p.z());
}

// This could be optimized by storing things, and there is also symmetry
// that could eliminate rotations, but that seems unnecessary for now.
Eigen::Vector3d Obb::corner(size_t pos)
{
    assert(pos < 8);
    Eigen::Vector3d v((pos & 1) ? -m_hx : m_hx,
                      (pos & 2) ? -m_hy : m_hy,
                      (pos & 4) ? -m_hz : m_hz);
    v = rotate(v, m_quat);
    v += m_p;
    return v;
}

Segment Obb::segment(size_t pos)
{
    assert(pos < 12);

    std::array<std::pair<std::size_t, std::size_t>, 12> segs
    {{
        {0, 2}, {2, 6}, {6, 4}, {4, 0},
        {1, 3}, {3, 7}, {7, 5}, {5, 1},
        {0, 1}, {2, 3}, {4, 5}, {6, 7}
    }};
    return { corner(segs[pos].first), corner(segs[pos].second) };
}

// For this to work both this and the clip box must be in the same cartesian
// system.
bool Obb::intersect(Obb c)
{
    // Translate both boxes so that this box is at the origin.
    c.m_p -= m_p;

    // Rotate the clip box by the inverse of this box's rotation to
    // bring it to the same relative location as this box unrotated.
    c.m_p = rotate(c.m_p, m_quat.inverse());

    // BOX3D representation of this OBB (translated to 0, 0, 0)
    BOX3D box(-m_hx, -m_hy, -m_hz, m_hx, m_hy, m_hz);

    // If any of the clip box corners are in this box, we're done.
    // While we're at it, store the min/max of corner points.
    Eigen::Vector3d pmin((std::numeric_limits<double>::max)(),
        (std::numeric_limits<double>::max)(),
        (std::numeric_limits<double>::max)());
    Eigen::Vector3d pmax((std::numeric_limits<double>::lowest)(),
        (std::numeric_limits<double>::lowest)(),
        (std::numeric_limits<double>::lowest)());
    for (size_t i = 0; i < 8; ++i)
    {
        Eigen::Vector3d corner = c.corner(i);
        if (box.contains(corner.x(), corner.y(), corner.z()))
            return true;

        pmax.x() = (std::max)(pmax.x(), corner.x());
        pmin.x() = (std::min)(pmin.x(), corner.x());
        pmax.y() = (std::max)(pmax.y(), corner.y());
        pmin.y() = (std::min)(pmin.y(), corner.y());
        pmax.z() = (std::max)(pmax.z(), corner.z());
        pmin.z() = (std::min)(pmin.z(), corner.z());
    }
std::cerr << "Not all corners contained!\n";

    // If the clip box surrounds this box, we're done.
    if (pmax.x() > m_hx && pmin.x() < -m_hx &&
        pmax.y() > m_hy && pmin.y() < -m_hy &&
        pmax.z() > m_hz && pmin.z() < -m_hz)
        return true;
std::cerr << "Not surrounded!\n";

    // If any of the segments that make up the clip region intersect
    // this normalized box, we're done.
    for (size_t i = 0; i < 12; ++i)
    {
        Segment s = c.segment(i);
        if (intersectNormalized(s))
            ;
//            return true;
    }

    // No intersection.
    return false;
}

// Note that this is just here to support the above.
bool Obb::intersectNormalized(const Segment& seg)
{
    Eigen::Vector3d p0 = seg.first;
    Eigen::Vector3d p1 = seg.second;

    // These represent both points on the faces of this box and
    // normal vectors to those faces.
    const size_t numFaces = 6;
    std::array<Eigen::Vector3d, numFaces> faces
    {{
        {m_hx, 0, 0},
        {-m_hx, 0, 0},
        {0, m_hy, 0},
        {0, -m_hy, 0},
        {0, 0, m_hz},
        {0, 0, -m_hz}
    }};

    std::array<BOX2D, 3> boxes
    {{
        {-m_hy, -m_hz, m_hy, m_hz},
        {-m_hx, -m_hz, m_hx, m_hz},
        {-m_hx, -m_hy, m_hx, m_hy}
    }};

    // Find the 3D intersection point of the segment and each of the faces.
    // Convert to a 2D point WRT the face and see if the point is in the 2D
    // face.
    for (size_t i = 0; i < numFaces; ++i)
    {
        Eigen::Vector3d face = faces[i];

        Eigen::Vector3d v1 = face - p0;
        Eigen::Vector3d v2 = p1 - p0;
        //ABELL div by 0.
        double t = v1.dot(face) / v2.dot(face);
        Eigen::Vector3d isect = t * (p1 -p0) + p0;
        if (t < 0 || t > 1)
            continue;

        // Convert our 3d point to a 2d one, ignoring the dimension
        // in the direction of the normal.
        double coord[2];
        size_t pos = 0;
        BOX2D *box;
        for (size_t i = 0; i < 3; ++i)
        {
            if (face[i])
                box = &boxes[i];
            else
                coord[pos++] = isect[i];
        }
        if (box->contains(coord[0], coord[1]))
        {
            std::cerr << "t = " << t << "!\n";
            std::cerr << "Intersection point = " << isect.transpose() << "!\n";
            std::cerr << "Box bounds[" << i << "] = " << *box << "!\n";
            std::cerr << "Coord check = " << coord[0] << "/" << coord[1] << "!\n";
            std::cerr << "\n";
//            return true;
        }
        else
            std::cerr << "Doesn't contain - t = " << t << "!\n\n";
    }

    return false;
}

} // namespace i3s
} // namespace pdal
