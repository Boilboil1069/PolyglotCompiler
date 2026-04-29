// physics_step.rs — Semi-implicit Euler integrator for a 1-D rigid body.
// Part of the PolyglotCompiler sample matrix.

#![allow(dead_code)]

pub struct Body {
    pub position: f64,
    pub velocity: f64,
}

pub fn advance(body: &mut Body, accel: f64, dt: f64) {
    body.velocity += accel * dt;
    body.position += body.velocity * dt;
}

pub fn distance(body: &Body) -> f64 {
    body.position.abs()
}

