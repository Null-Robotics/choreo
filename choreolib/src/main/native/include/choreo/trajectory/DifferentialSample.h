// Copyright (c) Choreo contributors

#pragma once

#include <functional>
#include <type_traits>

#include <Eigen/Core>
#include <frc/geometry/Pose2d.h>
#include <frc/kinematics/ChassisSpeeds.h>
#include <frc/system/NumericalIntegration.h>
#include <units/acceleration.h>
#include <units/angle.h>
#include <units/angular_acceleration.h>
#include <units/angular_velocity.h>
#include <units/force.h>
#include <units/length.h>
#include <units/time.h>
#include <units/velocity.h>
#include <wpi/MathExtras.h>
#include <wpi/json_fwd.h>

#include "choreo/util/AllianceFlipperUtil.h"

namespace choreo {

/**
 * A single differential drive robot sample in a Trajectory.
 */
class DifferentialSample {
 public:
  /**
   * Constructs a DifferentialSample that is defaulted.
   */
  constexpr DifferentialSample() = default;

  /**
   * Constructs a DifferentialSample with the specified parameters.
   *
   * @param timestamp The timestamp of this sample, relative to the beginning of
   * the trajectory.
   * @param x The X position of the sample
   * @param y The Y position of the sample
   * @param heading The heading of the sample, with 0 being in the +X direction.
   * @param vl The velocity of the left wheels
   * @param vr The velocity of the right wheels
   * @param omega The chassis angular velocity
   * @param al The acceleration of the left wheels
   * @param ar The acceleration of the left wheels
   * @param fl The force of the left wheels
   * @param fr The force of the right wheels
   */
  constexpr DifferentialSample(units::second_t timestamp, units::meter_t x,
                               units::meter_t y, units::radian_t heading,
                               units::meters_per_second_t vl,
                               units::meters_per_second_t vr,
                               units::radians_per_second_t omega,
                               units::meters_per_second_squared_t al,
                               units::meters_per_second_squared_t ar,
                               units::newton_t fl, units::newton_t fr)
      : timestamp{timestamp},
        x{x},
        y{y},
        heading{heading},
        vl{vl},
        vr{vr},
        omega{omega},
        al{al},
        ar{ar},
        fl{fl},
        fr{fr} {}

  /**
   * Gets the timestamp of the DifferentialSample.
   *
   * @return The timestamp.
   */
  units::second_t GetTimestamp() const { return timestamp; }

  /**
   * Gets the Pose2d of the DifferentialSample.
   *
   * @return The pose.
   */
  constexpr frc::Pose2d GetPose() const {
    return frc::Pose2d{x, y, frc::Rotation2d{heading}};
  }

  /**
   * Gets the field-relative chassis speeds of the DifferentialSample.
   *
   * @return The field-relative chassis speeds.
   */
  constexpr frc::ChassisSpeeds GetChassisSpeeds() const {
    return frc::ChassisSpeeds{(vl + vr) / 2.0, 0_mps, omega};
  }

  /**
   * Returns the current sample offset by a the time offset passed in.
   *
   * @param timeStampOffset time to move sample by
   * @return DifferentialSample that is moved forward by the offset
   */
  constexpr DifferentialSample OffsetBy(units::second_t timeStampOffset) const {
    return DifferentialSample{timestamp + timeStampOffset,
                              x,
                              y,
                              heading,
                              vl,
                              vr,
                              omega,
                              al,
                              ar,
                              fl,
                              fr};
  }

  /**
   * Interpolates between endValue and this by t
   *
   * @param endValue the end interpolated value
   * @param t time to move sample by
   * @return the interpolated sample
   */
  DifferentialSample Interpolate(const DifferentialSample& endValue,
                                 units::second_t t) const {
    units::scalar_t scale = (t - timestamp) / (endValue.timestamp - timestamp);

    // Integrate the acceleration to get the rest of the state, since linearly
    // interpolating the state gives an inaccurate result if the accelerations
    // are changing between states
    Eigen::Vector<double, 6> initialState{x.value(),       y.value(),
                                          heading.value(), vl.value(),
                                          vr.value(),      omega.value()};

    auto width = (vr - vl) / omega;

    // FIXME: this means the function cant be constexpr without c++23
    std::function<Eigen::Vector<double, 6>(Eigen::Vector<double, 6>,
                                           Eigen::Vector<double, 2>)>
        f = [width](Eigen::Vector<double, 6> state,
                    Eigen::Vector<double, 2> input) {
          //  state =  [x, y, θ, vₗ, vᵣ, ω]
          //  input =  [aₗ, aᵣ]
          //
          //  v = (vₗ + vᵣ)/2
          //  ω = (vᵣ − vₗ)/width
          //  α = (aᵣ − aₗ)/width
          //
          //  ẋ = v cosθ
          //  ẏ = v sinθ
          //  θ̇ = ω
          //  v̇ₗ = aₗ
          //  v̇ᵣ = aᵣ
          //  ω̇ = α
          auto θ = state(2, 0);
          auto vl = state(3, 0);
          auto vr = state(4, 0);
          auto ω = state(5, 0);
          auto al = input(0, 0);
          auto ar = input(1, 0);
          auto v = (vl + vr) / 2;
          auto α = (ar - al) / width.value();
          return Eigen::Vector<double, 6>{
              v * std::cos(θ), v * std::sin(θ), ω, al, ar, α};
        };

    units::second_t τ = t - timestamp;
    auto sample =
        frc::RKDP(f, initialState, Eigen::Vector<double, 2>(al, ar), τ);

    auto dt = endValue.timestamp - timestamp;
    auto jl = (endValue.al - al) / dt;
    auto jr = (endValue.ar - ar) / dt;

    return DifferentialSample{
        wpi::Lerp(timestamp, endValue.timestamp, scale),
        units::meter_t{sample(0, 0)},
        units::meter_t{sample(1, 0)},
        units::radian_t{sample(2, 0)},
        units::meters_per_second_t{sample(3, 0)},
        units::meters_per_second_t{sample(4, 0)},
        units::radians_per_second_t{sample(5, 0)},
        al + jl * τ,
        ar + jr * τ,
        wpi::Lerp(fl, endValue.fl, scale),
        wpi::Lerp(fr, endValue.fr, scale),
    };
  }

  /**
   * Returns the current sample flipped based on the field year.
   *
   * @tparam Year The field year.
   * @return DifferentialSample that is flipped based on the field layout.
   */
  template <int Year = util::kDefaultYear>
  constexpr DifferentialSample Flipped() const {
    constexpr auto flipper = choreo::util::GetFlipperForYear<Year>();
    if constexpr (flipper.isMirrored) {
      return DifferentialSample(timestamp, flipper.FlipX(x), flipper.FlipY(y),
                                flipper.FlipHeading(heading), vr, vl, -omega,
                                ar, al, fr, fl);
    } else {
      return DifferentialSample(timestamp, flipper.FlipX(x), flipper.FlipY(y),
                                flipper.FlipHeading(heading), vl, vr, omega, al,
                                ar, fl, fr);
    }
  }

  /**
   * DifferentialSample equality operator.
   *
   * @param other The other DifferentialSample.
   * @return True for equality.
   */
  constexpr bool operator==(const DifferentialSample& other) const {
    constexpr double epsilon = 1e-6;

    auto compare_units = [epsilon](const auto& a, const auto& b) {
      using UnitType =
          std::remove_const_t<std::remove_reference_t<decltype(a)>>;
      return units::math::abs(a - b) < UnitType(epsilon);
    };

    return compare_units(timestamp, other.timestamp) &&
           compare_units(x, other.x) && compare_units(y, other.y) &&
           compare_units(heading, other.heading) &&
           compare_units(vl, other.vl) && compare_units(vr, other.vr) &&
           compare_units(omega, other.omega) && compare_units(al, other.al) &&
           compare_units(ar, other.ar) && compare_units(fl, other.fl) &&
           compare_units(fr, other.fr);
  }

  /// The timestamp of this sample relative to the beginning of the trajectory.
  units::second_t timestamp = 0_s;

  /// The X position of the sample relative to the blue alliance wall origin.
  units::meter_t x = 0_m;

  /// The Y position of the sample relative to the blue alliance wall origin.
  units::meter_t y = 0_m;

  /// The heading of the sample, with 0 being in the +X direction.
  units::radian_t heading = 0_rad;

  /// The velocity of the left wheels.
  units::meters_per_second_t vl = 0_mps;

  /// The velocity of the right wheels.
  units::meters_per_second_t vr = 0_mps;

  /// The chassis angular velocity
  units::radians_per_second_t omega = 0_rad_per_s;

  /// The acceleration of the left wheels.
  units::meters_per_second_squared_t al = 0_mps_sq;

  /// The acceleration of the right wheels.
  units::meters_per_second_squared_t ar = 0_mps_sq;

  /// The force of the left wheels.
  units::newton_t fl = 0_N;

  /// The force of the right wheels.
  units::newton_t fr = 0_N;
};

void to_json(wpi::json& json, const DifferentialSample& trajectorySample);
void from_json(const wpi::json& json, DifferentialSample& trajectorySample);

}  // namespace choreo

#include "choreo/trajectory/struct/DifferentialSampleStruct.h"
