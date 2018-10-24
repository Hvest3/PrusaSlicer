#include <numeric>
#include "SLASupportTree.hpp"
#include "SLABoilerPlate.hpp"
#include "SLASpatIndex.hpp"

#include "Model.hpp"

namespace Slic3r {
namespace sla {

using Coordf = double;
using Portion = std::tuple<double, double>;
inline Portion make_portion(double a, double b) {
    return std::make_tuple(a, b);
}

template<class Vec> double distance(const Vec& pp1, const Vec& pp2) {
    auto p = pp2 - pp1;
    return distance(p);
}

template<class Vec> double distance(const Vec& p) {
    return std::sqrt(p.transpose() * p);
}

Contour3D sphere(double rho, Portion portion = make_portion(0.0, 2.0*PI),
                 double fa=(2*PI/360)) {

    Contour3D ret;

    // prohibit close to zero radius
    if(rho <= 1e-6 && rho >= -1e-6) return ret;

    auto& vertices = ret.points;
    auto& facets = ret.indices;

    // Algorithm:
    // Add points one-by-one to the sphere grid and form facets using relative
    // coordinates. Sphere is composed effectively of a mesh of stacked circles.

    // adjust via rounding to get an even multiple for any provided angle.
    double angle = (2*PI / floor(2*PI / fa));

    // Ring to be scaled to generate the steps of the sphere
    std::vector<double> ring;

    for (double i = 0; i < 2*PI; i+=angle) ring.emplace_back(i);

    const auto sbegin = size_t(2*std::get<0>(portion)/angle);
    const auto send = size_t(2*std::get<1>(portion)/angle);

    const size_t steps = ring.size();
    const double increment = (double)(1.0 / (double)steps);

    // special case: first ring connects to 0,0,0
    // insert and form facets.
    if(sbegin == 0)
        vertices.emplace_back(Vec3d(0.0, 0.0, -rho + increment*sbegin*2.0*rho));

    auto id = coord_t(vertices.size());
    for (size_t i = 0; i < ring.size(); i++) {
        // Fixed scaling
        const double z = -rho + increment*rho*2.0 * (sbegin + 1.0);
        // radius of the circle for this step.
        const double r = sqrt(abs(rho*rho - z*z));
        Vec2d b = Eigen::Rotation2Dd(ring[i]) * Eigen::Vector2d(0, r);
        vertices.emplace_back(Vec3d(b(0), b(1), z));

        if(sbegin == 0)
        facets.emplace_back((i == 0) ? Vec3crd(coord_t(ring.size()), 0, 1) :
                                       Vec3crd(id - 1, 0, id));
        ++ id;
    }

    // General case: insert and form facets for each step,
    // joining it to the ring below it.
    for (size_t s = sbegin + 2; s < send - 1; s++) {
        const double z = -rho + increment*(double)s*2.0*rho;
        const double r = sqrt(abs(rho*rho - z*z));

        for (size_t i = 0; i < ring.size(); i++) {
            Vec2d b = Eigen::Rotation2Dd(ring[i]) * Eigen::Vector2d(0, r);
            vertices.emplace_back(Vec3d(b(0), b(1), z));
            auto id_ringsize = coord_t(id - ring.size());
            if (i == 0) {
                // wrap around
                facets.emplace_back(Vec3crd(id - 1, id,
                                            id + coord_t(ring.size() - 1)));
                facets.emplace_back(Vec3crd(id - 1, id_ringsize, id));
            } else {
                facets.emplace_back(Vec3crd(id_ringsize - 1, id_ringsize, id));
                facets.emplace_back(Vec3crd(id - 1, id_ringsize - 1, id));
            }
            id++;
        }
    }

    // special case: last ring connects to 0,0,rho*2.0
    // only form facets.
    if(send >= size_t(2*PI / angle)) {
        vertices.emplace_back(Vec3d(0.0, 0.0, -rho + increment*send*2.0*rho));
        for (size_t i = 0; i < ring.size(); i++) {
            auto id_ringsize = coord_t(id - ring.size());
            if (i == 0) {
                // third vertex is on the other side of the ring.
                facets.emplace_back(Vec3crd(id - 1, id_ringsize, id));
            } else {
                auto ci = coord_t(id_ringsize + i);
                facets.emplace_back(Vec3crd(ci - 1, ci, id));
            }
        }
    }
    id++;

    return ret;
}

Contour3D cylinder(double r, double h, double fa=(2*PI/360)) {
    Contour3D ret;

    auto& vertices = ret.points;
    auto& facets = ret.indices;

    // 2 special vertices, top and bottom center, rest are relative to this
    vertices.emplace_back(Vec3d(0.0, 0.0, 0.0));
    vertices.emplace_back(Vec3d(0.0, 0.0, h));

    // adjust via rounding to get an even multiple for any provided angle.
    double angle = (2*PI / floor(2*PI / fa));

    // for each line along the polygon approximating the top/bottom of the
    // circle, generate four points and four facets (2 for the wall, 2 for the
    // top and bottom.
    // Special case: Last line shares 2 vertices with the first line.
    auto id = coord_t(vertices.size() - 1);
    vertices.emplace_back(Vec3d(sin(0) * r , cos(0) * r, 0));
    vertices.emplace_back(Vec3d(sin(0) * r , cos(0) * r, h));
    for (double i = 0; i < 2*PI; i+=angle) {
        Vec2d p = Eigen::Rotation2Dd(i) * Eigen::Vector2d(0, r);
        vertices.emplace_back(Vec3d(p(0), p(1), 0.));
        vertices.emplace_back(Vec3d(p(0), p(1), h));
        id = coord_t(vertices.size() - 1);
        facets.emplace_back(Vec3crd( 0, id - 1, id - 3)); // top
        facets.emplace_back(Vec3crd(id,      1, id - 2)); // bottom
        facets.emplace_back(Vec3crd(id, id - 2, id - 3)); // upper-right of side
        facets.emplace_back(Vec3crd(id, id - 3, id - 1)); // bottom-left of side
    }
    // Connect the last set of vertices with the first.
    facets.emplace_back(Vec3crd( 2, 0, id - 1));
    facets.emplace_back(Vec3crd( 1, 3,     id));
    facets.emplace_back(Vec3crd(id, 3,      2));
    facets.emplace_back(Vec3crd(id, 2, id - 1));

    return ret;
}

struct Head {
    Contour3D mesh;

    size_t steps = 45;
    Vec3d dir = {0, 0, -1};
    Vec3d tr = {0, 0, 0};

    double r_back_mm = 1;
    double r_pin_mm = 0.5;
    double width_mm = 2;

    struct Tail {
        Contour3D mesh;
        size_t steps = 45;
        double length = 1.6;
    } tail;

    Head(double r_big_mm,
         double r_small_mm,
         double length_mm,
         Vec3d direction = {0, 0, -1},    // direction (normal to the "ass" )
         Vec3d offset = {0, 0, 0},        // displacement
         const size_t circlesteps = 45):
            steps(circlesteps), dir(direction), tr(offset),
            r_back_mm(r_big_mm), r_pin_mm(r_small_mm), width_mm(length_mm)
    {

        // We create two spheres which will be connected with a robe that fits
        // both circles perfectly.

        // Set up the model detail level
        const double detail = 2*PI/steps;

        // We don't generate whole circles. Instead, we generate only the portions
        // which are visible (not covered by the robe)
        // To know the exact portion of the bottom and top circles we need to use
        // some rules of tangent circles from which we can derive (using simple
        // triangles the following relations:

        // The height of the whole mesh
        const double h = r_big_mm + r_small_mm + width_mm;
        double phi = PI/2 - std::acos( (r_big_mm - r_small_mm) / h );

        // To generate a whole circle we would pass a portion of (0, Pi)
        // To generate only a half horizontal circle we can pass (0, Pi/2)
        // The calculated phi is an offset to the half circles needed to smooth
        // the transition from the circle to the robe geometry

        auto&& s1 = sphere(r_big_mm, make_portion(0, PI/2 + phi), detail);
        auto&& s2 = sphere(r_small_mm, make_portion(PI/2 + phi, PI), detail);

        for(auto& p : s2.points) z(p) += h;

        mesh.merge(s1);
        mesh.merge(s2);

        for(size_t idx1 = s1.points.size() - steps, idx2 = s1.points.size();
            idx1 < s1.points.size() - 1;
            idx1++, idx2++)
        {
            coord_t i1s1 = coord_t(idx1), i1s2 = coord_t(idx2);
            coord_t i2s1 = i1s1 + 1, i2s2 = i1s2 + 1;

            mesh.indices.emplace_back(i1s1, i2s1, i2s2);
            mesh.indices.emplace_back(i1s1, i2s2, i1s2);
        }

        auto i1s1 = coord_t(s1.points.size()) - steps;
        auto i2s1 = coord_t(s1.points.size()) - 1;
        auto i1s2 = coord_t(s1.points.size());
        auto i2s2 = coord_t(s1.points.size()) + steps - 1;

        mesh.indices.emplace_back(i2s2, i2s1, i1s1);
        mesh.indices.emplace_back(i1s2, i2s2, i1s1);

        // To simplify further processing, we translate the mesh so that the
        // last vertex of the pointing sphere (the pinpoint) will be at (0,0,0)
        for(auto& p : mesh.points) { z(p) -= (h + r_small_mm); }

        tail.length = 0.8*width_mm;
    }

    void transform()
    {
        using Quaternion = Eigen::Quaternion<double>;

        // We rotate the head to the specified direction The head's pointing
        // side is facing upwards so this means that it would hold a support
        // point with a normal pointing straight down. This is the reason of
        // the -1 z coordinate
        auto quatern = Quaternion::FromTwoVectors(Vec3d{0, 0, -1}, dir);

        for(auto& p : mesh.points) p = quatern * p + tr;
    }

    double fullwidth() const {
        return 2*r_pin_mm + width_mm + 2*r_back_mm;
    }

    Vec3d junction_point() const {
        return tr + (2*r_pin_mm + width_mm + r_back_mm)*dir;
    }

    double request_pillar_radius(double radius) const {
        return radius > 0 && radius < r_back_mm ? radius : r_back_mm * 0.65;
    }

    void add_tail(double length = -1, double radius = -1) {
        auto& cntr = tail.mesh;
        Head& head = *this;
        if(length > 0) tail.length = length;

        cntr.points.reserve(2*steps);

        auto h = head.r_back_mm + 2*head.r_pin_mm + head.width_mm;
        Vec3d c = head.tr + head.dir * h;

        double r = head.r_back_mm * 0.9;
        double r_low = request_pillar_radius(radius);

        double a = 2*PI/steps;
        double z = c(2);
        for(int i = 0; i < steps; ++i) {
            double phi = i*a;
            double x = c(0) + r*std::cos(phi);
            double y = c(1) + r*std::sin(phi);
            cntr.points.emplace_back(x, y, z);
        }

        for(int i = 0; i < steps; ++i) {
            double phi = i*a;
            double lx = c(0) + r_low*std::cos(phi);
            double ly = c(1) + r_low*std::sin(phi);
            cntr.points.emplace_back(lx, ly, z - tail.length);
        }

        cntr.indices.reserve(2*steps);
        auto offs = steps;
        for(int i = 0; i < steps - 1; ++i) {
            cntr.indices.emplace_back(i, i + offs, offs + i + 1);
            cntr.indices.emplace_back(i, offs + i + 1, i + 1);
        }

        auto last = steps - 1;
        cntr.indices.emplace_back(0, last, offs);
        cntr.indices.emplace_back(last, offs + last, offs);
    }
};

struct Pillar {
    Contour3D mesh;
    Contour3D base;
    double r = 1;
    size_t steps = 0;
    Vec3d endpoint;
    std::reference_wrapper<const Head> headref;

    Pillar(const Head& head, const Vec3d& endp, double radius = 1) :
        endpoint(endp), headref(std::cref(head))
    {
        steps = head.steps;

        r = head.request_pillar_radius(radius);

        auto& points = mesh.points; points.reserve(head.tail.steps*2);
        points.insert(points.end(),
                      head.tail.mesh.points.begin() + steps,
                      head.tail.mesh.points.end()
                      );

        for(auto it = head.tail.mesh.points.begin() + steps;
            it != head.tail.mesh.points.end();
            ++it)
        {
            auto& s = *it;
            points.emplace_back(s(0), s(1), endp(2));
        }

        auto& indices = mesh.indices;
        auto offs = steps;
        for(int i = 0; i < steps - 1; ++i) {
            indices.emplace_back(i, i + offs, offs + i + 1);
            indices.emplace_back(i, offs + i + 1, i + 1);
        }

        auto last = steps - 1;
        indices.emplace_back(0, last, offs);
        indices.emplace_back(last, offs + last, offs);
    }

    void add_base(double height = 3, double radius = 2) {
        if(height <= 0) return;

        if(radius < r ) radius = r;

        double a = 2*PI/steps;
        double z = endpoint(2) + height;

        for(int i = 0; i < steps; ++i) {
            double phi = i*a;
            double x = endpoint(0) + r*std::cos(phi);
            double y = endpoint(1) + r*std::sin(phi);
            base.points.emplace_back(x, y, z);
        }

        for(int i = 0; i < steps; ++i) {
            double phi = i*a;
            double x = endpoint(0) + radius*std::cos(phi);
            double y = endpoint(1) + radius*std::sin(phi);
            base.points.emplace_back(x, y, z - height);
        }

        auto ep = endpoint; ep(2) += height;
        base.points.emplace_back(endpoint);
        base.points.emplace_back(ep);

        auto& indices = base.indices;
        auto hcenter = base.points.size() - 1;
        auto lcenter = base.points.size() - 2;
        auto offs = steps;
        for(int i = 0; i < steps - 1; ++i) {
            indices.emplace_back(i, i + offs, offs + i + 1);
            indices.emplace_back(i, offs + i + 1, i + 1);
            indices.emplace_back(i, i + 1, hcenter);
            indices.emplace_back(lcenter, offs + i + 1, offs + i);
        }

        auto last = steps - 1;
        indices.emplace_back(0, last, offs);
        indices.emplace_back(last, offs + last, offs);
        indices.emplace_back(hcenter, last, 0);
        indices.emplace_back(offs, offs + last, lcenter);

    }

    bool has_base() const { return !base.points.empty(); }
};

struct Junction {
    Contour3D mesh;
    double r = 1;
    size_t steps = 45;
    Vec3d pos;

    Junction(const Vec3d& tr, double r_mm, size_t stepnum = 45):
        r(r_mm), steps(stepnum), pos(tr)
    {
        mesh = sphere(r_mm, make_portion(0, PI), 2*PI/steps);
        for(auto& p : mesh.points) p += tr;
    }
};

struct Bridge {
    Contour3D mesh;
    double r = 0.8;

    Bridge(const Junction& j1, const Junction& j2, double r_mm = 0.8):
        r(r_mm)
    {
        using Quaternion = Eigen::Quaternion<double>;
        Vec3d dir = (j2.pos - j1.pos).normalized();
        double d = distance(j2.pos, j1.pos);

        mesh = cylinder(r, d, 2*PI / 45);

        auto quater = Quaternion::FromTwoVectors(Vec3d{0,0,1}, dir);
        for(auto& p : mesh.points) p = quater * p + j1.pos;
    }

    Bridge(const Head& h, const Junction& j2, double r_mm = 0.8):
        r(r_mm)
    {
//        double headsize = 2*h.r_pin_mm + h.width_mm + h.r_back_mm;
//        Vec3d hp = h.tr + h.dir * headsize;
//        Vec3d dir = (j2.pos - hp).normalized();

    }

    Bridge(const Junction& j, const Pillar& cl) {}

};

EigenMesh3D to_eigenmesh(const Contour3D& cntr) {
    EigenMesh3D emesh;

    auto& V = emesh.V;
    auto& F = emesh.F;

    V.resize(cntr.points.size(), 3);
    F.resize(cntr.indices.size(), 3);

    for (int i = 0; i < V.rows(); ++i) {
        V.row(i) = cntr.points[i];
        F.row(i) = cntr.indices[i];
    }

    return emesh;
}

void create_head(TriangleMesh& out, double r1_mm, double r2_mm, double width_mm)
{
    Head head(r1_mm, r2_mm, width_mm, {0, std::sqrt(0.5), -std::sqrt(0.5)},
              {0, 0, 30});
    out.merge(mesh(head.mesh));
    out.merge(mesh(head.tail.mesh));

    Pillar cst(head, {0, 0, 0});
    cst.add_base();

    out.merge(mesh(cst.mesh));
    out.merge(mesh(cst.base));
}

//enum class ClusterType: double {
static const double /*constexpr*/ D_SP   = 0.1;
static const double /*constexpr*/ D_BRIDGED_TRIO  = 3;
//static const double /*constexpr*/ D_SSDH = 1.0;  // Same stick different heads
//static const double /*constexpr*/ D_DHCS = 3.0;  // different heads, connected sticks
//static const double /*constexpr*/ D_DH3S = 5.0;  // different heads, additional 3rd stick
//static const double /*constexpr*/ D_DHDS = 8.0;  // different heads, different stick
//};

enum { // For indexing Eigen vectors as v(X), v(Y), v(Z) instead of numbers
  X, Y, Z
};

EigenMesh3D to_eigenmesh(const Model& model) {
    TriangleMesh combined_mesh;

    for(ModelObject *o : model.objects) {
        TriangleMesh tmp = o->raw_mesh();
        for(ModelInstance * inst: o->instances) {
            TriangleMesh ttmp(tmp);
            inst->transform_mesh(&ttmp);
            combined_mesh.merge(ttmp);
        }
    }

    const stl_file& stl = combined_mesh.stl;

    EigenMesh3D outmesh;
    auto& V = outmesh.V;
    auto& F = outmesh.F;

    V.resize(3*stl.stats.number_of_facets, 3);
    F.resize(stl.stats.number_of_facets, 3);
    for (unsigned int i=0; i<stl.stats.number_of_facets; ++i) {
        const stl_facet* facet = stl.facet_start+i;
        V(3*i+0, 0) = facet->vertex[0](0); V(3*i+0, 1) =
                facet->vertex[0](1); V(3*i+0, 2) = facet->vertex[0](2);
        V(3*i+1, 0) = facet->vertex[1](0); V(3*i+1, 1) =
                facet->vertex[1](1); V(3*i+1, 2) = facet->vertex[1](2);
        V(3*i+2, 0) = facet->vertex[2](0); V(3*i+2, 1) =
                facet->vertex[2](1); V(3*i+2, 2) = facet->vertex[2](2);

        F(i, 0) = 3*i+0;
        F(i, 1) = 3*i+1;
        F(i, 2) = 3*i+2;
    }

    return outmesh;
}

Vec3d model_coord(const ModelInstance& object, const Vec3f& mesh_coord) {
    return object.transform_vector(mesh_coord.cast<double>());
}

PointSet support_points(const Model& model) {
    size_t sum = 0;
    for(auto *o : model.objects)
        sum += o->instances.size() * o->sla_support_points.size();

    PointSet ret(sum, 3);

    for(ModelObject *o : model.objects)
        for(ModelInstance *inst : o->instances) {
            int i = 0;
            for(Vec3f& msource : o->sla_support_points) {
                ret.row(i++) = model_coord(*inst, msource);
            }
        }

    return ret;
}

double ray_mesh_intersect(const Vec3d& s,
                          const Vec3d& dir,
                          const EigenMesh3D& m);

PointSet normals(const PointSet& points, const EigenMesh3D& mesh);

Vec2d to_vec2(const Vec3d& v3) {
    return {v3(0), v3(1)};
}

bool operator==(const SpatElement& e1, const SpatElement& e2) {
    return e1.second == e2.second;
}

// Clustering a set of points by the given criteria
ClusteredPoints cluster(
        const PointSet& points,
        std::function<bool(const SpatElement&, const SpatElement&)> pred,
        unsigned max_points = 0);

class SLASupportTree::Impl {
    std::vector<Head> m_heads;
    std::vector<Pillar> m_pillars;
    std::vector<Junction> m_junctions;
    std::vector<Bridge> m_bridges;
public:

    template<class...Args> Head& add_head(Args&&... args) {
        m_heads.emplace_back(std::forward<Args>(args)...);
        return m_heads.back();
    }

    template<class...Args> Pillar& add_pillar(Args&&... args) {
        m_pillars.emplace_back(std::forward<Args>(args)...);
        return m_pillars.back();
    }

    template<class...Args> Junction& add_junction(Args&&... args) {
        m_junctions.emplace_back(std::forward<Args>(args)...);
        return m_junctions.back();
    }

    template<class...Args> Bridge& add_bridge(Args&&... args) {
        m_bridges.emplace_back(std::forward<Args>(args)...);
        return m_bridges.back();
    }

    const std::vector<Head>& heads() const { return m_heads; }
    Head& head(size_t idx) { return m_heads[idx]; }
    const std::vector<Pillar>& pillars() const { return m_pillars; }
    const std::vector<Bridge>& bridges() const { return m_bridges; }
    const std::vector<Junction>& junctions() const { return m_junctions; }
};

template<class DistFn>
long cluster_centroid(const ClusterEl& clust,
                      std::function<Vec3d(size_t)> pointfn,
                      DistFn df)
{
    switch(clust.size()) {
    case 0: /* empty cluster */ return -1;
    case 1: /* only one element */ return 0;
    case 2: /* if two elements, there is no center */ return 0;
    default: ;
    }

    // The function works by calculating for each point the average distance
    // from all the other points in the cluster. We create a selector bitmask of
    // the same size as the cluster. The bitmask will have two true bits and
    // false bits for the rest of items and we will loop through all the
    // permutations of the bitmask (combinations of two points). Get the
    // distance for the two points and add the distance to the averages.
    // The point with the smallest average than wins.

    std::vector<bool> sel(clust.size(), false);   // create full zero bitmask
    std::fill(sel.end() - 2, sel.end(), true);    // insert the two ones
    std::vector<double> avgs(clust.size(), 0.0);  // store the average distances

    do {
        std::array<size_t, 2> idx;
        for(size_t i = 0, j = 0; i < clust.size(); i++) if(sel[i]) idx[j++] = i;

        double d = df(pointfn(clust[idx[0]]),
                      pointfn(clust[idx[1]]));

        // add the distance to the sums for both associated points
        for(auto i : idx) avgs[i] += d;

        // now continue with the next permutation of the bitmask with two 1s
    } while(std::next_permutation(sel.begin(), sel.end()));

    // Divide by point size in the cluster to get the average (may be redundant)
    for(auto& a : avgs) a /= clust.size();

    // get the lowest average distance and return the index
    auto minit = std::min_element(avgs.begin(), avgs.end());
    return long(minit - avgs.begin());
}

/**
 * This function will calculate the convex hull of the input point set and
 * return the indices of those points belonging to the chull in the right
 * (counter clockwise) order. The input is also the set of indices and a
 * functor to get the actual point form the index.
 */
ClusterEl pts_convex_hull(const ClusterEl& inpts,
                          std::function<Vec2d(unsigned)> pfn)
{
    using Point = Vec2d;
    using std::vector;

    static const double ERR = 1e-6;

    auto orientation = [](const Point& p, const Point& q, const Point& r)
    {
        double val = (q(Y) - p(Y)) * (r(X) - q(X)) -
                     (q(X) - p(X)) * (r(Y) - q(Y));

        if (std::abs(val) < ERR) return 0;  // collinear
        return (val > ERR)? 1: 2; // clock or counterclockwise
    };

    size_t n = inpts.size();

    if (n < 3) return inpts;

    // Initialize Result
    ClusterEl hull;
    vector<Point> points; points.reserve(n);
    for(auto i : inpts) points.emplace_back(pfn(i));

    // Find the leftmost point
    int l = 0;
    for (int i = 1; i < n; i++) {
        if(std::abs(points[i](X) - points[l](X)) < ERR) {
            if(points[i](Y) < points[l](Y)) l = i;
        }
        else if (points[i](X) < points[l](X)) l = i;
    }


    // Start from leftmost point, keep moving counterclockwise
    // until reach the start point again.  This loop runs O(h)
    // times where h is number of points in result or output.
    int p = l;
    do
    {
        // Add current point to result
        hull.push_back(p);

        // Search for a point 'q' such that orientation(p, x,
        // q) is counterclockwise for all points 'x'. The idea
        // is to keep track of last visited most counterclock-
        // wise point in q. If any point 'i' is more counterclock-
        // wise than q, then update q.
        int q = (p+1)%n;
        for (int i = 0; i < n; i++)
        {
           // If i is more counterclockwise than current q, then
           // update q
           if (orientation(points[p], points[i], points[q]) == 2) q = i;
        }

        // Now q is the most counterclockwise with respect to p
        // Set p as q for next iteration, so that q is added to
        // result 'hull'
        p = q;

    } while (p != l);  // While we don't come to first point

    return hull;
}

bool SLASupportTree::generate(const Model& model,
                              const SupportConfig& cfg,
                              const Controller& ctl) {
    auto points = support_points(model);
    auto mesh =   to_eigenmesh(model);

    PointSet filtered_points;
    PointSet filtered_normals;
    PointSet head_positions;
    PointSet headless_positions;

    using IndexSet = std::vector<unsigned>;

    // Distances from head positions to ground or mesh touch points
    std::vector<double> head_heights;

    // Indices of those who touch the ground
    IndexSet ground_heads;

    // Indices of those who don't touch the ground
    IndexSet noground_heads;

    ClusteredPoints ground_connectors;

    auto gnd_head_pt = [&ground_heads, &head_positions] (size_t idx) {
        return Vec3d(head_positions.row(ground_heads[idx]));
    };

    using Result = SLASupportTree::Impl;

    Result& result = *m_impl;

    enum Steps {
        BEGIN,
        FILTER,
        PINHEADS,
        CLASSIFY,
        ROUTING_GROUND,
        ROUTING_NONGROUND,
        HEADLESS,
        DONE,
        HALT,
        ABORT,
        NUM_STEPS
        //...
    };

    auto filterfn = [] (
            const SupportConfig& cfg,
            const PointSet& points,
            const EigenMesh3D& mesh,
            PointSet& filt_pts,
            PointSet& filt_norm,
            PointSet& head_pos,
            PointSet& headless_pos)
    {

        /* ******************************************************** */
        /* Filtering step                                           */
        /* ******************************************************** */

        // Get the points that are too close to each other and keep only the
        // first one
        auto aliases = cluster(points,
                               [cfg](const SpatElement& p,
                               const SpatElement& se){
            return distance(p.first, se.first) < D_SP;
        }, 2);

        filt_pts.resize(aliases.size(), 3);
        int count = 0;
        for(auto& a : aliases) {
            // Here we keep only the front point of the cluster. TODO: centroid
            filt_pts.row(count++) = points.row(a.front());
        }

        // calculate the normals to the triangles belonging to filtered points
        auto nmls = sla::normals(filt_pts, mesh);

        filt_norm.resize(count, 3);
        head_pos.resize(count, 3);
        headless_pos.resize(count, 3);

        // Not all of the support points have to be a valid position for
        // support creation. The angle may be inappropriate or there may
        // not be enough space for the pinhead. Filtering is applied for
        // these reasons.

        int pcount = 0, hlcount = 0;
        for(int i = 0; i < count; i++) {
            auto n = nmls.row(i);

            // for all normals we generate the spherical coordinates and
            // saturate the polar angle to 45 degrees from the bottom then
            // convert back to standard coordinates to get the new normal.
            // Then we just create a quaternion from the two normals
            // (Quaternion::FromTwoVectors) and apply the rotation to the
            // arrow head.

            double z = n(2);
            double r = 1.0;     // for normalized vector
            double polar = std::acos(z / r);
            double azimuth = std::atan2(n(1), n(0));

            if(polar >= PI / 2) { // skip if the tilt is not sane

                // We saturate the polar angle to 3pi/4
                polar = std::max(polar, 3*PI / 4);

                // Reassemble the now corrected normal
                Vec3d nn(std::cos(azimuth) * std::sin(polar),
                         std::sin(azimuth) * std::sin(polar),
                         std::cos(polar));

                // save the head (pinpoint) position
                Vec3d hp = filt_pts.row(i);

                // the full width of the head
                double w = cfg.head_width_mm +
                           cfg.head_back_radius_mm +
                           2*cfg.head_front_radius_mm;

                // We should shoot a ray in the direction of the pinhead and
                // see if there is enough space for it
                double t = ray_mesh_intersect(hp + 0.1*nn, nn, mesh);

                if(t > 2*w || std::isinf(t)) {
                    // 2*w because of lower and upper pinhead

                    head_pos.row(pcount) = hp;

                    // save the verified and corrected normal
                    filt_norm.row(pcount) = nn;

                    ++pcount;
                } else {
                    headless_pos.row(hlcount++) = hp;
                }
            }
        }

        head_pos.conservativeResize(pcount, Eigen::NoChange);
        filt_norm.conservativeResize(pcount, Eigen::NoChange);
        headless_pos.conservativeResize(hlcount, Eigen::NoChange);
    };

    // Function to write the pinheads into the result
    auto pinheadfn = [] (
            const SupportConfig& cfg,
            PointSet& head_pos,
            PointSet& nmls,
            Result& result
            )
    {

        /* ******************************************************** */
        /* Generating Pinheads                                      */
        /* ******************************************************** */

        for (int i = 0; i < head_pos.rows(); ++i) {
            result.add_head(
                        cfg.head_back_radius_mm,
                        cfg.head_front_radius_mm,
                        cfg.head_width_mm,
                        nmls.row(i),         // dir
                        head_pos.row(i)      // displacement
                        );
        }
    };

    // &filtered_points, &head_positions, &result, &mesh,
    // &gndidx, &gndheight, &nogndidx, cfg
    auto classifyfn = [] (
            const SupportConfig& cfg,
            const EigenMesh3D& mesh,
            PointSet& head_pos,
            IndexSet& gndidx,
            IndexSet& nogndidx,
            std::vector<double>& gndheight,
            ClusteredPoints& ground_clusters,
            Result& result
            ) {

        /* ******************************************************** */
        /* Classification                                           */
        /* ******************************************************** */

        // We should first get the heads that reach the ground directly
        gndheight.reserve(head_pos.rows());
        gndidx.reserve(head_pos.rows());
        nogndidx.reserve(head_pos.rows());

        for(unsigned i = 0; i < head_pos.rows(); i++) {
            auto& head = result.heads()[i];

            Vec3d dir(0, 0, -1);
            Vec3d startpoint = head.junction_point();

            double t = ray_mesh_intersect(startpoint, dir, mesh);

            gndheight.emplace_back(t);

            if(std::isinf(t)) gndidx.emplace_back(i);
            else nogndidx.emplace_back(i);
        }

        PointSet gnd(gndidx.size(), 3);

        for(size_t i = 0; i < gndidx.size(); i++)
            gnd.row(i) = head_pos.row(gndidx[i]);

        // We want to search for clusters of points that are far enough from
        // each other in the XY plane to generate the column stick base
        auto d_base = 4*cfg.base_radius_mm;
        ground_clusters = cluster(gnd,
            [d_base](const SpatElement& p, const SpatElement& s){
            return distance(Vec2d(p.first(0), p.first(1)),
                            Vec2d(s.first(0), s.first(1))) < d_base;
        }, 4); // max 3 heads to connect to one centroid

        for(auto idx : nogndidx) {
            auto& head = result.head(idx);
            head.transform();
            head.add_tail();

            double gh = gndheight[idx];
            Vec3d headend = head.junction_point();

            Head base_head(cfg.head_back_radius_mm,
                 cfg.head_front_radius_mm,
                 cfg.head_width_mm,
                 {0.0, 0.0, 1.0},
                 {headend(0), headend(1), headend(2) - gh - head.r_pin_mm});

            base_head.transform();

            double hl = head.fullwidth() - head.r_back_mm;

            Pillar cs(head,
                      Vec3d{headend(0), headend(1), headend(2) - gh + hl},
                      cfg.pillar_radius_mm);

            cs.base = base_head.mesh;
            result.add_pillar(cs);

        }
    };

    auto routing_ground_fn = [gnd_head_pt](
            const SupportConfig& cfg,
            const ClusteredPoints& gnd_clusters,
            const IndexSet& gndidx,
            const EigenMesh3D& emesh,
            Result& result)
    {
        const double hbr = cfg.head_back_radius_mm;

        ClusterEl cl_centroids;
        cl_centroids.reserve(gnd_clusters.size());

        // Connect closely coupled support points to one pillar if there is
        // enough downward space.
        for(auto cl : gnd_clusters) {

            unsigned cidx = cluster_centroid(cl, gnd_head_pt,
                                           [](const Vec3d& p1, const Vec3d& p2)
            {
                return distance(Vec2d(p1(X), p1(Y)), Vec2d(p2(X), p2(Y)));
            });

            cl_centroids.emplace_back(cl[cidx]);

            size_t index_to_heads = gndidx[cl[cidx]];
            auto& head = result.head(index_to_heads);

            head.add_tail();
            head.transform();

            Vec3d startpoint = head.junction_point();
            auto endpoint = startpoint; endpoint(Z) = 0;

            Pillar cs(head, endpoint, cfg.pillar_radius_mm);
            cs.add_base(cfg.base_height_mm, cfg.base_radius_mm);

            result.add_pillar(cs);

            cl.erase(cl.begin() + cidx);

            for(auto c : cl) { // point in current cluster
                auto& sidehead = result.head(gndidx[c]);
                sidehead.transform();
                sidehead.add_tail();

                // get an appropriate radius for the pillar
                double r_pillar = sidehead.request_pillar_radius(
                            cfg.pillar_radius_mm);

                // The distance in z direction by which the junctions on the
                // pillar will be placed subsequently.
                double jstep = sidehead.fullwidth();

                // connect to the main column by junction
                auto jp = sidehead.junction_point();

                // move to the next junction point
                jp(Z) -= jstep;

                // Now we want to hit the central pillar with a "tilt"ed bridge
                // stick and (optionally) place a junction point there.
                auto jh = head.junction_point();
                // with simple trigonometry, we calculate the z coordinate on
                // the main pillar. Distance is between the two pillars in 2d:
                double d = distance(Vec2d{jp(X), jp(Y)},
                                    Vec2d{jh(X), jh(Y)});

                Vec3d jn(jh(X), jh(Y), jp(Z) + d*sin(-cfg.tilt));

                if(jn(Z) > 0) {
                    // if the junction on the main pillar above ground
                    auto& jjp = result.add_junction(jp, hbr);
                    result.add_pillar(sidehead, jp, cfg.pillar_radius_mm);

                    auto&& jjn = result.add_junction(jn, hbr);
                    result.add_bridge(jjp, jjn, r_pillar);
                } else {
                    // if there is no space for the connection, a dedicated
                    // pillar is created for all the support points in the
                    // cluster. This is the case with dense support points
                    // close to the ground.

                    jp(Z) = 0;
                    Pillar sidecs(sidehead, jp, cfg.pillar_radius_mm);
                    sidecs.add_base(cfg.base_height_mm, cfg.base_radius_mm);
                    result.add_pillar(sidecs);
                }
            }
        }

        // We will break down the pillar positions in 2D into concentric rings.
        // Connecting the pillars belonging to the same ring will prevent
        // bridges from crossing each other. After bridging the rings we can
        // create bridges between the rings without the possibility of crossing
        // bridges.

        SpatIndex junction_index;
        for(auto ej : enumerate(result.junctions())) { // fill the spatial index
            auto& p = ej.value.pos;
            junction_index.insert({p(X), p(Y), 0}, unsigned(ej.index));
        }

        ClusterEl rem = cl_centroids;
        while(!rem.empty()) {
            std::sort(rem.begin(), rem.end());

            auto ring = pts_convex_hull(rem,
                                        [gnd_head_pt](unsigned i) {
                auto& p = gnd_head_pt(i);
                return Vec2d(p(X), p(Y)); // project to 2D in along Z axis
            });

            std::cout << "ring: \n";
            for(auto r : ring) std::cout << r << " ";
            std::cout << std::endl;

            // now the ring has to be connected with bridge sticks

            for(auto it = ring.begin(), next = std::next(it);
                next != ring.end();
                ++it, ++next)
            {
                auto idx = unsigned(*it);
                const Pillar& pillar = result.pillars()[*it];
                const Pillar& nextpillar = result.pillars()[*next];

                double d = 2*pillar.r;
                const Vec3d& p = pillar.endpoint;
                Vec3d  pp{p(X), p(Y), 0};

                // we must find the already created junctions on current pillar
                auto juncs = junction_index.query([pp, d](const SpatElement& se)
                {
                    return distance(pp, se.first) < d;
                });

                Vec3d sj;
                if(juncs.empty()) {
                    // No junctions on the pillar so far. Using the head.
                    sj = pillar.headref.get().junction_point();
                } else {
                    // search for the highest junction in z direction
                    auto juncit = std::max_element(juncs.begin(), juncs.end(),
                                                   [](const SpatElement& se1,
                                                      const SpatElement& se2){
                        return se1.first(2) < se2.first(2);
                    });
                    sj = result.junctions()[juncit->second].pos;
                }

                // try to create new bridge to the nearest pillar.
                // if it bumps into the model, we should try other starting
                // points and if that fails as well than leave it be and
                // continue with the second nearest junction and so on.

                Vec3d ej = nextpillar.endpoint;
                double pillar_dist = distance(Vec2d{sj(X), sj(Y)},
                                              Vec2d{ej(X), ej(Y)});
                ej(Z) = sj(Z) + pillar_dist * std::sin(-cfg.tilt);

                // now we have the two new junction points on the pillars, we
                // should check if they can be safely connected:
                double chkd = ray_mesh_intersect(sj, (ej - sj).normalized(),
                                                 emesh);

                double nstartz = nextpillar.headref.get().junction_point()(Z);
                while(nextpillar.endpoint(Z) < ej(Z) &&
                      pillar.endpoint(Z) < sj(Z))
                {
                    if(chkd >= pillar_dist && nstartz > ej(Z)) {
                        auto& jS = result.add_junction(sj, hbr);
                        auto& jE = result.add_junction(ej, hbr);
                        result.add_bridge(jS, jE, pillar.r);
                    }

                    sj.swap(ej);
                    ej(Z) = sj(Z) + pillar_dist * std::sin(-cfg.tilt);
                    chkd = ray_mesh_intersect(sj, (ej - sj).normalized(), emesh);
                }
            }

            auto sring = ring; ClusterEl tmp;
            std::sort(sring.begin(), sring.end());
            std::set_difference(rem.begin(), rem.end(),
                                sring.begin(), sring.end(),
                                std::back_inserter(tmp));
            rem.swap(tmp);
        }


//        // Now connect the created pillars with each other creating a network
//        // of interconnected supports
//        SpatIndex junction_index;
//        SpatIndex pillar_index;

//        for(auto ej : enumerate(result.junctions())) {
//            auto& p = ej.value.pos;
//            junction_index.insert({p(0), p(1), 0}, unsigned(ej.index));
//        }

//        for(auto ej : enumerate(result.pillars())) {
//            auto& p = ej.value.endpoint;
//            pillar_index.insert({p(0), p(1), 0}, unsigned(ej.index));
//        }

//        std::set<size_t> ipillars;


//        for(auto it = ipillars.begin(); it != ipillars.end();)
//        {
//            size_t idx = *it;
//            const Pillar& pillar = result.pillars()[idx];

//            auto& p = pillar.endpoint;
//            auto pp = Vec3d{p(0), p(1), 0};
//            auto sp = std::make_pair(pp, unsigned(idx));
//            pillar_index.remove(sp);

//            auto qv = pillar_index.nearest(pp, 1);

//            // no other pillars to connect to, quit the loop
//            if(qv.empty()) break;

//            SpatElement q = qv.front();
//            const Pillar& nearpillar = result.pillars()[q.second];
//            double d = 2*pillar.r;

//            // we must find the already created junctions current pillar
//            auto juncs = junction_index.query([pp, d](const SpatElement& se)
//            {
//                return distance(pp, se.first) < d;
//            });

//            Vec3d sj;
//            if(juncs.empty()) {
//                // No junctions on the pillar so far. Using the head.
//                sj = pillar.headref.get().junction_point();
//            } else {
//                // search for the lowest junction in z direction
//                auto juncit = std::min_element(juncs.begin(), juncs.end(),
//                                               [](const SpatElement& se1,
//                                                  const SpatElement& se2){
//                    return se1.first(2) < se2.first(2);
//                });
//                sj = result.junctions()[juncit->second].pos;
//            }

//            // try to create new bridge to the nearest pillar.
//            // if it bumps into the model, we should try other starting
//            // points and if that fails as well than leave it be and
//            // continue with the second nearest junction and so on.

//            // calculate z coord of new junction
//            sj(2) -= cfg.junction_distance;

//            Vec3d ej = nearpillar.endpoint;
//            double pillar_dist = distance(Vec2d{sj(0), sj(1)},
//                                          Vec2d{ej(0), ej(1)});
//            ej(2) = sj(2) + pillar_dist * std::sin(-cfg.tilt);

//            // now we have the two new junction points on the pillars, we
//            // should check if they can be safely connected:
//            double chkd = ray_mesh_intersect(sj, (ej - sj).normalized(),
//                                             emesh);

//            double nstartz = nearpillar.headref.get().junction_point()(2);
//            while(nearpillar.endpoint(2) < ej(2) &&
//                  pillar.endpoint(2) < sj(2))
//            {
//                if(chkd >= pillar_dist && nstartz > ej(2)) {
//                    auto& jS = result.add_junction(sj, hbr);
//                    auto& jE = result.add_junction(ej, hbr);
//                    result.add_bridge(jS, jE, pillar.r);
//                }

//                sj.swap(ej);
//                ej(2) = sj(2) + pillar_dist * std::sin(-cfg.tilt);
//                chkd = ray_mesh_intersect(sj, (ej - sj).normalized(), emesh);
//            }

//            // if the nearest pillar connects to ground, continue with that
//            if(nearpillar.has_base()) {
//                ipillars.erase(it);
//                it = ipillars.find(q.second);
//            } else {
//                it = ipillars.erase(it);

//                // remove the floating (nearest) pillar from the index
//                auto np = Vec3d{q.first(0), q.first(1), 0};
//                pillar_index.remove(std::make_pair(np, unsigned(q.second)));
//            }
//        }
    };

    using std::ref;
    using std::cref;
    using std::bind;

    // Here we can easily track what goes in and what comes out of each step:
    // (see the cref-s as inputs and ref-s as outputs)
    std::array<std::function<void()>, NUM_STEPS> program = {
    [] () {
        // Begin
        // clear up the shared data
    },

    // Filtering unnecessary support points
    bind(filterfn, cref(cfg), cref(points), cref(mesh),
         ref(filtered_points), ref(filtered_normals),
         ref(head_positions),  ref(headless_positions)),

    // Pinhead generation
    bind(pinheadfn, cref(cfg),
             ref(head_positions), ref(filtered_normals), ref(result)),

    // Classification of support points
    bind(classifyfn, cref(cfg), cref(mesh),
             ref(head_positions), ref(ground_heads), ref(noground_heads),
             ref(head_heights), ref(ground_connectors), ref(result)),

    // Routing ground connecting clusters
    bind(routing_ground_fn,
         cref(cfg), cref(ground_connectors), cref(ground_heads), cref(mesh),
         ref(result)),

    [] () {
        // Routing non ground connecting clusters
    },
    [] () {
        // Processing headless support points
    },
    [] () {
        // Done
    },
    [] () {
        // Halt
    },
    [] () {
        // Abort
    }
    };

    Steps pc = BEGIN, pc_prev = BEGIN;

    auto progress = [&ctl, &model, &pc, &pc_prev] () {
        static const std::array<std::string, NUM_STEPS> stepstr {
            ""
            "Filtering",
            "Generate pinheads"
            "Classification",
            "Routing to ground",
            "Routing supports to model surface",
            "Processing small holes"
            "Done",
            "Halt",
            "Abort"
        };

        static const std::array<unsigned, NUM_STEPS> stepstate {
            0,
            10,
            30,
            50,
            60,
            70,
            80,
            100,
            0,
            0
        };

        auto cmd = ctl.nextcmd(/* block if: */ pc == HALT);

        switch(cmd) {
        case Controller::Cmd::START_RESUME:
            switch(pc) {
            case BEGIN: pc = FILTER; break;
            case FILTER: pc = PINHEADS; break;
            case PINHEADS: pc = CLASSIFY; break;
            case CLASSIFY: pc = ROUTING_GROUND; break;
            case ROUTING_GROUND: pc = ROUTING_NONGROUND; break;
            case ROUTING_NONGROUND: pc = HEADLESS; break;
            case HEADLESS: pc = DONE; break;
            case HALT: pc = pc_prev; break;
            case DONE:
            case ABORT: break; // we should never get here
            }
            ctl.statuscb(stepstate[pc], stepstr[pc]);
            break;
        case Controller::Cmd::PAUSE:
            pc_prev = pc;
            pc = HALT;
            ctl.statuscb(stepstate[pc], stepstr[pc]);
            break;
        case Controller::Cmd::STOP:
            pc = ABORT; ctl.statuscb(stepstate[pc], stepstr[pc]); break;
        case Controller::Cmd::SYNCH:
            pc = BEGIN;
            // TODO
        }
    };

    // Just here we run the computation...
    while(pc < DONE || pc == HALT) {
        progress();
        program[pc]();
    }

    return pc == ABORT;
}

SLASupportTree::SLASupportTree(): m_impl(new Impl()) {}

SLASupportTree::SLASupportTree(const SLASupportTree &c):
    m_impl( new Impl(*c.m_impl)) {}

SLASupportTree &SLASupportTree::operator=(const SLASupportTree &c)
{
    m_impl = make_unique<Impl>(*c.m_impl);
    return *this;
}

SLASupportTree::~SLASupportTree() {}

void add_sla_supports(Model &model,
                      const SupportConfig &cfg,
                      const Controller &ctl)
{
    SLASupportTree _stree;
    _stree.generate(model, cfg, ctl);

    SLASupportTree::Impl& stree = _stree.get();
    ModelObject* o = model.add_object();
    o->add_instance();

    for(auto& head : stree.heads()) {
        o->add_volume(mesh(head.mesh));
        o->add_volume(mesh(head.tail.mesh));
    }

    for(auto& stick : stree.pillars()) {
        o->add_volume(mesh(stick.mesh));
        o->add_volume(mesh(stick.base));
    }

    for(auto& j : stree.junctions()) {
        o->add_volume(mesh(j.mesh));
    }

    for(auto& bs : stree.bridges()) {
        o->add_volume(mesh(bs.mesh));
    }

}

}
}