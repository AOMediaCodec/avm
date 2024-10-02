use std::fmt;

use serde::{Deserialize, Serialize};

#[derive(Debug, PartialEq, Eq, Clone, Copy, Hash, Serialize, Deserialize)]
pub enum Plane {
    Y,
    U,
    V,
}
impl Plane {
    pub fn as_str(&self) -> &str {
        match self {
            Plane::Y => "Y plane",
            Plane::U => "U plane",
            Plane::V => "V plane",
        }
    }

    pub fn from_i32(i: i32) -> Self {
        match i {
            0 => Plane::Y,
            1 => Plane::U,
            2 => Plane::V,
            _ => panic!("Bad plane id: {i}"),
        }
    }

    pub fn to_i32(&self) -> i32 {
        match self {
            Plane::Y => 0,
            Plane::U => 1,
            Plane::V => 2,
        }
    }

    pub fn to_usize(&self) -> usize {
        self.to_i32() as usize
    }

    pub fn is_chroma(&self) -> bool {
        match self {
            Plane::Y => false,
            Plane::U | Plane::V => true,
        }
    }

    pub fn subsampled(&self, dimension: i32, subsample: u8) -> i32 {
        if self.is_chroma() {
            if subsample == 0 {
                dimension
            } else {
                (dimension + 1) / 2
            }
        } else {
            dimension
        }
    }
}

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum PlaneType {
    Planar(Plane),
    #[default]
    Rgb,
}

impl PlaneType {
    // For partition tree selection
    pub fn use_chroma(&self) -> bool {
        match self {
            PlaneType::Rgb | PlaneType::Planar(Plane::Y) => false,
            PlaneType::Planar(Plane::U) | PlaneType::Planar(Plane::V) => true,
        }
    }

    pub fn to_plane(&self) -> Plane {
        match self {
            PlaneType::Rgb => Plane::Y,
            PlaneType::Planar(plane) => *plane,
        }
    }
}

impl fmt::Display for PlaneType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let text = match self {
            PlaneType::Planar(Plane::Y) => "Y",
            PlaneType::Planar(Plane::U) => "U",
            PlaneType::Planar(Plane::V) => "V",
            PlaneType::Rgb => "YUV",
        };
        write!(f, "{text}")
    }
}
