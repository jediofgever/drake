#pragma once

#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "drake/common/autodiff.h"
#include "drake/common/drake_optional.h"
#include "drake/common/sorted_pair.h"
#include "drake/geometry/geometry_ids.h"
#include "drake/geometry/query_results/contact_surface.h"
#include "drake/geometry/query_results/penetration_as_point_pair.h"
#include "drake/geometry/query_results/signed_distance_pair.h"
#include "drake/geometry/query_results/signed_distance_to_point.h"
#include "drake/geometry/shape_specification.h"
#include "drake/math/rigid_transform.h"

namespace drake {
namespace geometry {

template <typename T> class GeometryState;

namespace internal {

#ifndef DRAKE_DOXYGEN_CXX
// This provides GeometryState limited "friend" access to ProximityEngine for
// the purpose of collision filters.
class GeometryStateCollisionFilterAttorney;
#endif

/** The underlying engine for performing geometric _proximity_ queries.
 It owns the geometry instances and, once it has been provided with the poses
 of the geometry, it provides geometric queries on that geometry.

 Proximity queries span a range of types, including:

   - penetration
   - distance
   - ray-intersection

 @tparam T The scalar type. Must be a valid Eigen scalar.

 Instantiated templates for the following kinds of T's are provided:

 - double
 - AutoDiffXd

 They are already available to link against in the containing library.
 No other values for T are currently supported.

 @internal Historically, this replaces the DrakeCollision::Model class.  */
template <typename T>
class ProximityEngine {
 public:
  ProximityEngine();
  ~ProximityEngine();

  /** Construct a deep copy of the provided `other` engine.  */
  ProximityEngine(const ProximityEngine& other);

  /** Set `this` engine to be a deep copy of the `other` engine.  */
  ProximityEngine& operator=(const ProximityEngine& other);

  /** Construct an engine by moving the data of a source engine. The source
   engine will be returned to its default-initialized state.  */
  ProximityEngine(ProximityEngine&& other) noexcept;

  /** Move assign a source engine to this engine. The source
   engine will be returned to its default-initialized state.  */
  ProximityEngine& operator=(ProximityEngine&& other) noexcept;

  /** Returns an independent copy of this engine templated on the AutoDiffXd
   scalar type. If the engine is already an AutoDiffXd engine, it is equivalent
   to using the copy constructor to create a duplicate on the heap.  */
  std::unique_ptr<ProximityEngine<AutoDiffXd>> ToAutoDiffXd() const;

  /** @name Topology management */
  //@{

  /** Adds the given `shape` to the engine's _dynamic_ geometry.
   @param shape   The shape to add.
   @param id      The id of the geometry in SceneGraph to which this shape
                  belongs.  */
  void AddDynamicGeometry(const Shape& shape, GeometryId id);

  /** Adds the given `shape` to the engine's _anchored_ geometry.
   @param shape   The shape to add.
   @param X_WG    The pose of the shape in the world frame.
   @param id      The id of the geometry in SceneGraph to which this shape
                  belongs.  */
  void AddAnchoredGeometry(const Shape& shape,
                           const math::RigidTransformd& X_WG, GeometryId id);

  // TODO(SeanCurtis-TRI): Decide if knowing whether something is dynamic or not
  //  is *actually* sufficiently helpful to justify this act.
  /** Removes the given geometry indicated by `id` from the engine.
   @param id          The id of the geometry to be removed.
   @param is_dynamic  True if the geometry is dynamic, false if anchored.
   @throws std::logic_error if `id` does not refer to a geometry in this engine.
  */
  void RemoveGeometry(GeometryId id, bool is_dynamic);

  /** Reports the _total_ number of geometries in the engine -- dynamic and
   anchored (spanning all sources).  */
  int num_geometries() const;

  /** Reports the number of _dynamic_ geometries (spanning all sources).  */
  int num_dynamic() const;

  /** Reports the number of _anchored_ geometries (spanning all sources).  */
  int num_anchored() const;

  /** The distance (signed/unsigned/penetration distance) is generally computed
   from an iterative process. The distance_tolerance determines when the
   iterative process will terminate.
   As a rule of rule of thumb, one can generally assume that the answer will
   be within 10 * tol to the true answer.  */
  void set_distance_tolerance(double tol);

  double distance_tolerance() const;

  //@}

  /** Updates the poses for all of the _dynamic_ geometries in the engine.
   @param X_WGs     The poses of each geometry `G` measured and expressed in the
                    world frame `W` (including geometries which may *not* be
                    registered with the proximity engine or may not be
                    dynamic).
  */
  // TODO(SeanCurtis-TRI): I could do things here differently a number of ways:
  //  1. I could make this move semantics (or swap semantics).
  //  2. I could simply have a method that returns a mutable reference to such
  //    a vector and the caller sets values there directly.
  void UpdateWorldPoses(
      const std::unordered_map<GeometryId, math::RigidTransform<T>>& X_WGs);

  // ----------------------------------------------------------------------
  /**@name              Signed Distance Queries
  See @ref signed_distance_query "Signed Distance Query" for more details.  */

  //@{
  // TODO(SeanCurtis-TRI): Remove this documentation in favor of a link to the
  // public API.
  // NOTE: This maps to Model::ClosestPointsAllToAll().
  /** Determines the closest points between "all" pairs of bodies/elements. In
   this case, for a signed distance to be reported for geometry pair (A, B):

     - A and B cannot both be anchored.
     - The pair (A, B) cannot be marked as filtered.
     - The distance between A and B must be less than `max_distance`.

   For a geometry pair (A, B), the returned results will always be reported in
   a fixed order (e.g., always (A, B) and never (B, A)). The _basis_ for the
   ordering is arbitrary (and therefore undocumented), but guaranteed to be
   fixed and repeatable.

   @param[in] X_WGs           The pose of all geometries in World, keyed on
                              each geometry's GeometryId.
   @param[in] max_distance    The maximum distance between objects such that
                              they will be included in the results.
   @returns  A vector populated with per-object-pair signed distance values (and
             supporting data).
   */
  std::vector<SignedDistancePair<T>>
  ComputeSignedDistancePairwiseClosestPoints(
      const std::unordered_map<GeometryId, math::RigidTransform<T>>& X_WGs,
      const double max_distance) const;

  /** Performs work in support of GeometryState::ComputeSignedDistanceToPoint().
   @param[in] p_WQ            Position of a query point Q in world frame W.
   @param[in] X_WGs           The pose of all geometries in world, keyed by
                              each geometry's GeometryId.
   @param[in] threshold       Ignore any object beyond this distance.
   @retval signed_distances   A vector populated with per-object signed
                              distance and gradient vector.
                              See SignedDistanceToPoint for details.
   */
  std::vector<SignedDistanceToPoint<T>>
  ComputeSignedDistanceToPoint(
      const Vector3<T>& p_WQ,
      const std::unordered_map<GeometryId, math::RigidTransform<T>>& X_WGs,
      const double threshold = std::numeric_limits<double>::infinity()) const;
  //@}


  //----------------------------------------------------------------------------
  /** @name                Collision Queries

   These queries detect _collisions_ between geometry. Two geometries collide
   if they overlap each other and are not explicitly excluded through
   @ref collision_filter_concepts "collision filtering". These algorithms find
   those colliding cases, characterize them, and report the essential
   characteristics of that collision.

   Computes the penetrations across all pairs of geometries in the world.
   Only reports results for _penetrating_ geometries; if two geometries are
   not penetrating, there will be no result for that pair. Geometries whose
   surfaces are just touching (osculating) are not considered in penetration.
   Surfaces whose penetration is within an epsilon of osculation, are likewise
   not considered penetrating.

   These methods are affected by collision filtering; geometry pairs that
   have been filtered will not produce contacts, even if their collision
   geometry is penetrating.
   */

  //@{

  // NOTE: This maps to Model::ComputeMaximumDepthCollisionPoints().
  // The definition that touching is not penetrating is due to an FCL issue
  // described in https://github.com/flexible-collision-library/fcl/issues/375
  // and drake issue #10577. Once that is resolved, this definition can be
  // revisited (and ProximityEngineTest::Issue10577Regression_Osculation can
  // be updated).
  /**
   Computes the penetrations across all pairs of geometries in the world with
   the penetrations characterized by pairs of points (providing some measure
   of the penetration "depth" of the two objects), but _not_ the overlapping
   volume.

   For two penetrating geometries g_A and g_B, it is guaranteed that they will
   map to `id_A` and `id_B` in a fixed, repeatable manner.

   @returns A vector populated with all detected penetrations characterized as
            point pairs.  */
  std::vector<PenetrationAsPointPair<double>> ComputePointPairPenetration()
      const;

  /**
   Computes the intersections across all pairs of geometries in the world with
   the intersections characterized by contact surfaces (see ContactSurface).

   For two intersecting geometries g_A and g_B, it is guaranteed that they will
   map to `id_A` and `id_B` in a fixed, repeatable manner, where `id_A` and
   `id_B` are GeometryId's of geometries g_A and g_B respectively.

   @param[in] X_WGs           The pose of all geometries in world, keyed by
                              each geometry's GeometryId.
   @returns A vector populated with all detected intersections characterized as
            contact surfaces.  */
  std::vector<ContactSurface<T>> ComputeContactSurfaces(
      const std::unordered_map<GeometryId, math::RigidTransform<T>>& X_WGs)
      const;

  //@}

  /**
   Performs a broad-phase pass and returns a vector containing collision pair
   candidates. A pair in the returned set is not necessarily in contact, and
   further analysis must be done to confirm contact. A pair of geometries not
   present in the result is guaranteed not to be in contact.  */
  std::vector<SortedPair<GeometryId>> FindCollisionCandidates() const;

  /** @name               Collision filters

   This interface provides the mechanism through which pairs of geometries are
   removed from the "candidate pair set" for collision detection.

   See @ref scene_graph_collision_filtering "Scene Graph Collision Filtering"
   for more details.
   */
  //@{

  /** Excludes geometry pairs from collision evaluation by updating the
   candidate pair set `C = C - P`, where `P = {(gᵢ, gⱼ)}, ∀ gᵢ, gⱼ ∈ G` and
   `G = dynamic ⋃ anchored = {g₀, g₁, ..., gₙ}`.
   @param[in]   dynamic     The set of geometry ids for _dynamic_ geometries
                            for which no collisions can be reported.
   @param[in]   anchored    The set of geometry ids for _anchored_
                            geometries for which no collisions can be reported.
  */
  void ExcludeCollisionsWithin(
      const std::unordered_set<GeometryId>& dynamic,
      const std::unordered_set<GeometryId>& anchored);

  /** Excludes geometry pairs from collision evaluation by updating the
   candidate pair set `C = C - P`, where `P = {(a, b)}, ∀ a ∈ A, b ∈ B` and
   `A = dynamic1 ⋃ anchored1 = {a₀, a₁, ..., aₘ}` and
   `B = dynamic2 ⋃ anchored2 = {b₀, b₁, ..., bₙ}`. This does _not_
   preclude collisions between members of the _same_ set.   */
  void ExcludeCollisionsBetween(
      const std::unordered_set<GeometryId>& dynamic1,
      const std::unordered_set<GeometryId>& anchored1,
      const std::unordered_set<GeometryId>& dynamic2,
      const std::unordered_set<GeometryId>& anchored2);

  /** Reports true if the geometry pair (id1, id2) has been filtered from
   collision.  */
  bool CollisionFiltered(GeometryId id1, bool is_dynamic_1,
                         GeometryId id2, bool is_dynamic_2) const;
  //@}

 private:
  // Class to give GeometryState access to clique management.
  friend class GeometryStateCollisionFilterAttorney;

  // Retrieves the next available clique.
  int get_next_clique();

  // Assigns the given clique to the geometry indicated by `id`.
  // This is exposed via the GeometryStateCollisionFilterAttorney to allow
  // GeometryState to set up cliques between sibling geometries.
  void set_clique(GeometryId id, int clique);

  ////////////////////////////////////////////////////////////////////////////

  // Testing utilities:
  // These functions facilitate *limited* introspection into the engine state.
  // This enables unit tests to make assertions about pre- and post-operation
  // state.

  // Reports true if other is detectably a deep copy of this engine.
  bool IsDeepCopy(const ProximityEngine<T>& other) const;

  // Reveals what the next generated clique will be (without changing it).
  int peek_next_clique() const;

  // Reports the pose (X_WG) of the geometry with the given id.
  const math::RigidTransform<double> GetX_WG(GeometryId id,
                                             bool is_dynamic) const;

  ////////////////////////////////////////////////////////////////////////////

  // TODO(SeanCurtis-TRI): Pimpl + template implementation has proven
  // problematic. This gets around it but it isn't a reliable long-term
  // solution. Figure out how to make this work with unique_ptr or
  // copyable unique_ptr
  //
  // The implementation details.
  class Impl;
  Impl* impl_{};

  // Private constructor to use for scalar conversion.
  explicit ProximityEngine(Impl* impl);

  // Engine on one scalar can see the members of other engines.
  template <typename> friend class ProximityEngine;

  // Facilitate testing.
  friend class ProximityEngineTester;
};

#ifndef DRAKE_DOXYGEN_CXX
// This is an attorney-client pattern providing GeometryState limited access to
// the collision filtering mechanism of the ProximityEngine in order to be able
// to filter collisions between geometries affixed to the same frame.
//
// This class (and the supporting functions in ProximityEngine) are short-term
// hacks. SceneGraph needs to be able to exclude collisions between geometries
// affixed to the same frame. Using the public API would lead to a proliferation
// of cliques. This exploits knowledge of the underlying representation
// (cliques) to avoid egregious redundancy; the SceneGraph explicitly
// manipulates cliques. When the legacy collision filter mechanism is removed
// (and the cliques with it), this class and its supporting functions will
// likewise go.
// TODO(SeanCurtis-TRI): Delete this with the change in collision filtering
// mechanism.
class GeometryStateCollisionFilterAttorney {
 public:
  DRAKE_NO_COPY_NO_MOVE_NO_ASSIGN(GeometryStateCollisionFilterAttorney);
  GeometryStateCollisionFilterAttorney() = delete;

 private:
  template <typename T>
  friend class drake::geometry::GeometryState;

  // Allocates a unique, unused clique from the underlying engine's set of
  // cliques.
  template <typename T>
  static int get_next_clique(ProximityEngine<T>* engine) {
    return engine->get_next_clique();
  }

  // Assigns the given clique to the *dynamic* geometry indicated by the given
  // id. This function exists for one reason, and one reason only. To allow
  // GeometryState to automatically exclude pair (gᵢ, gⱼ) from collision if gᵢ
  // and gⱼ are affixed to the same frame.
  template <typename T>
  static void set_dynamic_geometry_clique(ProximityEngine<T>* engine,
                                          GeometryId geometry_index,
                                          int clique) {
    engine->set_clique(geometry_index, clique);
  }

  // Utility for GeometryState tests.
  template <typename T>
  static int peek_next_clique(const ProximityEngine<T>& engine) {
    return engine.peek_next_clique();
  }
};
#endif

}  // namespace internal
}  // namespace geometry
}  // namespace drake
