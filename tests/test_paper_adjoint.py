import cmath
import math
import unittest


class PaperAdjointTest(unittest.TestCase):
    def test_transmission_objective_complex_derivative(self):
        outlet = complex(3.0, 4.0)
        reference = complex(1.2, -1.6)
        target = 0.8

        def objective(value):
            transmission = abs(value) / abs(reference)
            return ((transmission - target) / target) ** 2

        transmission = abs(outlet) / abs(reference)
        derivative = (
            2.0 * (transmission - target)
            / (target * target * abs(reference) * abs(outlet))
            * outlet
        )
        step = 1.0e-6
        real_difference = (
            objective(outlet + step) - objective(outlet - step)
        ) / (2.0 * step)
        imaginary_difference = (
            objective(outlet + 1j * step) - objective(outlet - 1j * step)
        ) / (2.0 * step)
        self.assertTrue(math.isclose(
            derivative.real, real_difference, rel_tol=1.0e-9, abs_tol=1.0e-9))
        self.assertTrue(math.isclose(
            derivative.imag, imaginary_difference, rel_tol=1.0e-9, abs_tol=1.0e-9))

    def test_real_fft_adjoint_satisfies_transpose_identity(self):
        for sample_count in (7, 8):
            signal = [math.sin(0.37 * n) + 0.1 * n for n in range(sample_count)]
            spectrum = [
                sum(
                    signal[n]
                    * cmath.exp(-2j * math.pi * frequency * n / sample_count)
                    for n in range(sample_count)
                )
                for frequency in range(sample_count // 2 + 1)
            ]
            spectrum_derivative = [
                complex(0.2 + 0.13 * frequency, -0.17 + 0.09 * frequency)
                for frequency in range(len(spectrum))
            ]
            spectrum_derivative[0] = complex(spectrum_derivative[0].real, 0.0)
            if sample_count % 2 == 0:
                spectrum_derivative[-1] = complex(
                    spectrum_derivative[-1].real, 0.0)

            signal_derivative = []
            for sample in range(sample_count):
                value = spectrum_derivative[0].real
                for frequency in range(1, len(spectrum_derivative)):
                    if sample_count % 2 == 0 and frequency == sample_count // 2:
                        value += spectrum_derivative[frequency].real * (-1) ** sample
                        continue
                    angle = 2.0 * math.pi * frequency * sample / sample_count
                    value += spectrum_derivative[frequency].real * math.cos(angle)
                    value -= spectrum_derivative[frequency].imag * math.sin(angle)
                signal_derivative.append(value)

            frequency_inner_product = sum(
                derivative.real * value.real + derivative.imag * value.imag
                for derivative, value in zip(spectrum_derivative, spectrum)
            )
            time_inner_product = sum(
                derivative * value
                for derivative, value in zip(signal_derivative, signal)
            )
            self.assertTrue(math.isclose(
                frequency_inner_product,
                time_inner_product,
                rel_tol=1.0e-12,
                abs_tol=1.0e-12,
            ))

    def test_fft_newmark_adjoint_matches_centered_difference(self):
        steps = 12
        dt = 0.01
        beta = 0.25
        gamma = 0.5
        a1 = 1.0 - gamma / beta
        a2 = (1.0 - gamma / (2.0 * beta)) * dt
        a3 = gamma / (beta * dt)
        a4 = 1.0 / (beta * dt)
        a5 = 1.0 / (2.0 * beta) - 1.0
        a6 = 1.0 / (beta * dt * dt)
        window = [
            0.5 * (1.0 - math.cos(2.0 * math.pi * n / (steps - 1)))
            for n in range(steps)
        ]
        load = [math.sin(n * 0.7) + 0.2 for n in range(steps + 1)]

        def transform(signal):
            return [
                sum(
                    signal[n]
                    * cmath.exp(-2j * math.pi * frequency * n / steps)
                    for n in range(steps)
                )
                for frequency in range(steps // 2 + 1)
            ]

        def forward(design):
            mass = 2.0 + 0.3 * design
            damping = 0.4 + 0.2 * design
            stiffness = 5.0 - 0.1 * design
            displacement = velocity = 0.0
            acceleration = load[0] / mass
            states = [(displacement, velocity, acceleration)]
            effective = stiffness + a6 * mass + a3 * damping
            outlet = [displacement]
            for step in range(1, steps + 1):
                mass_state = (
                    a4 * velocity + a5 * acceleration + a6 * displacement
                )
                damping_state = (
                    -a1 * velocity - a2 * acceleration + a3 * displacement
                )
                next_displacement = (
                    load[step] + mass * mass_state + damping * damping_state
                ) / effective
                next_velocity = (
                    a1 * velocity
                    + a2 * acceleration
                    + a3 * (next_displacement - displacement)
                )
                next_acceleration = (
                    -a4 * velocity
                    - a5 * acceleration
                    + a6 * (next_displacement - displacement)
                )
                if step < steps:
                    outlet.append(next_displacement)
                displacement = next_displacement
                velocity = next_velocity
                acceleration = next_acceleration
                states.append((displacement, velocity, acceleration))

            spectrum = transform(
                [window[n] * outlet[n] for n in range(steps)])
            target = 0.13
            objective = (abs(spectrum[2]) - target) ** 2
            derivative = [0j for _ in spectrum]
            derivative[2] = (
                2.0 * (abs(spectrum[2]) - target)
                / abs(spectrum[2]) * spectrum[2]
            )
            return objective, (mass, damping, stiffness), states, derivative

        def reverse(design):
            objective, operators, states, spectrum_derivative = forward(design)
            mass, damping, stiffness = operators
            outlet_derivative = []
            for sample in range(steps):
                value = spectrum_derivative[0].real
                value += spectrum_derivative[-1].real * (-1) ** sample
                for frequency in range(1, len(spectrum_derivative) - 1):
                    angle = 2.0 * math.pi * frequency * sample / steps
                    value += spectrum_derivative[frequency].real * math.cos(angle)
                    value -= spectrum_derivative[frequency].imag * math.sin(angle)
                outlet_derivative.append(window[sample] * value)

            effective = stiffness + a6 * mass + a3 * damping
            displacement_bar = velocity_bar = acceleration_bar = 0.0
            adjoint = [0.0] * (steps + 1)
            for step in range(steps, 0, -1):
                if step < steps:
                    displacement_bar += outlet_derivative[step]
                previous_displacement_bar = (
                    -a3 * velocity_bar - a6 * acceleration_bar
                )
                previous_velocity_bar = (
                    a1 * velocity_bar - a4 * acceleration_bar
                )
                previous_acceleration_bar = (
                    a2 * velocity_bar - a5 * acceleration_bar
                )
                displacement_bar += a3 * velocity_bar + a6 * acceleration_bar
                adjoint[step] = displacement_bar / effective
                previous_displacement_bar += (
                    a6 * mass + a3 * damping
                ) * adjoint[step]
                previous_velocity_bar += (
                    a4 * mass - a1 * damping
                ) * adjoint[step]
                previous_acceleration_bar += (
                    a5 * mass - a2 * damping
                ) * adjoint[step]
                displacement_bar = previous_displacement_bar
                velocity_bar = previous_velocity_bar
                acceleration_bar = previous_acceleration_bar

            initial_adjoint = acceleration_bar / mass
            gradient = -initial_adjoint * 0.3 * states[0][2]
            for step in range(1, steps + 1):
                displacement = states[step][0]
                previous_displacement, previous_velocity, previous_acceleration = (
                    states[step - 1]
                )
                mass_state = (
                    a4 * previous_velocity
                    + a5 * previous_acceleration
                    + a6 * previous_displacement
                    - a6 * displacement
                )
                damping_state = (
                    -a1 * previous_velocity
                    - a2 * previous_acceleration
                    + a3 * previous_displacement
                    - a3 * displacement
                )
                gradient += adjoint[step] * (
                    0.3 * mass_state + 0.2 * damping_state + 0.1 * displacement
                )
            return objective, gradient

        design = 0.37
        objective, adjoint_gradient = reverse(design)
        perturbation = 1.0e-6
        finite_difference = (
            forward(design + perturbation)[0]
            - forward(design - perturbation)[0]
        ) / (2.0 * perturbation)

        self.assertTrue(math.isfinite(objective))
        self.assertTrue(
            math.isclose(
                adjoint_gradient,
                finite_difference,
                rel_tol=1.0e-7,
                abs_tol=1.0e-9,
            )
        )


if __name__ == "__main__":
    unittest.main()
