use crate::{
    CodingUnitContext, CodingUnitKind, EnumMappings, Frame, FrameError, PartitionContext, Plane, PlaneType, Spatial,
    SuperblockContext, SuperblockLocator, SymbolContext, TransformUnitContext,
};

pub enum ProtoEnumMapping {
    TransformType,
    EntropyCodingMode,
    InterpolationFilter,
    PredictionMode,
    UvPredictionMode,
    MotionMode,
    TransformSize,
    BlockSize,
    PartitionType,
    FrameType,
    TipMode,
    MotionVectorPrecision,
}

impl Frame {
    pub fn iter_coding_units(&self, kind: CodingUnitKind) -> impl Iterator<Item = CodingUnitContext> + '_ {
        self.iter_superblocks().flat_map(move |ctx| ctx.iter_coding_units(kind))
    }

    /// Whether this frame has separate luma and chroma partition trees (i.e. semi-decoupled partitioning - SDP).
    ///
    /// This is stored at the superblock level, but each superblock is assumed to have the same SDP setting.
    pub fn has_separate_chroma_partition_tree(&self) -> bool {
        if let Some(sb) = self.superblocks.first() {
            sb.has_separate_chroma_partition_tree
        } else {
            false
        }
    }

    pub fn coding_unit_kind(&self, plane_type: PlaneType) -> CodingUnitKind {
        match plane_type {
            PlaneType::Rgb => CodingUnitKind::Shared,
            PlaneType::Planar(Plane::Y) => {
                if self.has_separate_chroma_partition_tree() {
                    CodingUnitKind::LumaOnly
                } else {
                    CodingUnitKind::Shared
                }
            }
            PlaneType::Planar(Plane::U | Plane::V) => {
                if self.has_separate_chroma_partition_tree() {
                    CodingUnitKind::ChromaOnly
                } else {
                    CodingUnitKind::Shared
                }
            }
        }
    }

    pub fn iter_coding_unit_rects(&self, kind: CodingUnitKind) -> impl Iterator<Item = emath::Rect> + '_ {
        self.iter_coding_units(kind).map(|ctx| ctx.coding_unit.rect())
    }

    pub fn iter_transform_units(&self, plane: Plane) -> impl Iterator<Item = TransformUnitContext> {
        let kind = self.coding_unit_kind(PlaneType::Planar(plane));
        self.iter_coding_units(kind)
            .flat_map(move |ctx| ctx.iter_transform_units(plane))
    }

    pub fn iter_transform_rects(&self, plane: Plane) -> impl Iterator<Item = emath::Rect> + '_ {
        self.iter_transform_units(plane).map(|ctx| ctx.transform_unit.rect())
    }

    pub fn iter_superblocks(&self) -> impl Iterator<Item = SuperblockContext> {
        self.superblocks
            .iter()
            .enumerate()
            .map(|(i, superblock)| SuperblockContext {
                superblock,
                frame: self,
                locator: SuperblockLocator::new(i),
            })
    }

    pub fn iter_partitions(&self, kind: CodingUnitKind) -> impl Iterator<Item = PartitionContext> {
        self.iter_superblocks()
            .flat_map(move |superblock_context| superblock_context.iter_partitions(kind))
    }

    fn get_enum_mappings(&self) -> Result<&EnumMappings, FrameError> {
        self.enum_mappings
            .as_ref()
            .ok_or(FrameError::BadFrame("Missing enum mappings.".into()))
    }
    pub fn enum_lookup(&self, enum_type: ProtoEnumMapping, value: i32) -> Result<String, FrameError> {
        use FrameError::*;
        let enum_mappings = self.get_enum_mappings()?;
        match enum_type {
            ProtoEnumMapping::TransformType => enum_mappings
                .transform_type_mapping
                .get(&value)
                .ok_or(BadFrame(format!("Missing transform type value: {value}"))),
            ProtoEnumMapping::EntropyCodingMode => enum_mappings
                .entropy_coding_mode_mapping
                .get(&value)
                .ok_or(BadFrame(format!("Missing entropy coding mode value: {value}"))),
            ProtoEnumMapping::InterpolationFilter => enum_mappings
                .interpolation_filter_mapping
                .get(&value)
                .ok_or(BadFrame(format!("Missing interpolation filter value: {value}"))),
            ProtoEnumMapping::PredictionMode => enum_mappings
                .prediction_mode_mapping
                .get(&value)
                .ok_or(BadFrame(format!("Missing prediction mode value: {value}"))),
            ProtoEnumMapping::UvPredictionMode => enum_mappings
                .uv_prediction_mode_mapping
                .get(&value)
                .ok_or(BadFrame(format!("Missing UV prediction mode value: {value}"))),
            ProtoEnumMapping::MotionMode => enum_mappings
                .motion_mode_mapping
                .get(&value)
                .ok_or(BadFrame(format!("Missing motion mode value: {value}"))),
            ProtoEnumMapping::TransformSize => enum_mappings
                .transform_size_mapping
                .get(&value)
                .ok_or(BadFrame(format!("Missing transform size value: {value}"))),
            ProtoEnumMapping::BlockSize => enum_mappings
                .block_size_mapping
                .get(&value)
                .ok_or(BadFrame(format!("Missing block size value: {value}"))),
            ProtoEnumMapping::PartitionType => enum_mappings
                .partition_type_mapping
                .get(&value)
                .ok_or(BadFrame(format!("Missing partition type value: {value}"))),
            ProtoEnumMapping::FrameType => enum_mappings
                .frame_type_mapping
                .get(&value)
                .ok_or(BadFrame(format!("Missing frame type value: {value}"))),
            ProtoEnumMapping::TipMode => enum_mappings
                .tip_mode_mapping
                .get(&value)
                .ok_or(BadFrame(format!("Missing TIP mode value: {value}"))),
            ProtoEnumMapping::MotionVectorPrecision => enum_mappings
                .motion_vector_precision_mapping
                .get(&value)
                .ok_or(BadFrame(format!("Missing MV precision value: {value}"))),
        }
        .cloned()
    }

    pub fn iter_superblock_rects(&self) -> impl Iterator<Item = emath::Rect> + '_ {
        self.iter_superblocks()
            .map(|superblock_context| superblock_context.superblock.rect())
    }

    pub fn iter_symbols(&self) -> impl Iterator<Item = SymbolContext> {
        self.iter_superblocks()
            .flat_map(move |superblock_context| superblock_context.iter_symbols(None))
    }

    pub fn bit_depth(&self) -> u8 {
        self.frame_params
            .as_ref()
            .map_or(0, |frame_params| frame_params.bit_depth as u8)
    }

    pub fn decode_index(&self) -> usize {
        self.frame_params
            .as_ref()
            .map_or(0, |frame_params| frame_params.decode_index as usize)
    }

    pub fn display_index(&self) -> usize {
        self.frame_params
            .as_ref()
            .map_or(0, |frame_params| frame_params.display_index as usize)
    }

    pub fn frame_type_name(&self) -> String {
        if let Some(frame_params) = self.frame_params.as_ref() {
            let frame_type = frame_params.frame_type;
            if let Ok(name) = self.enum_lookup(ProtoEnumMapping::FrameType, frame_type) {
                return name;
            }
        }
        "UNKNOWN".into()
    }

    pub fn tip_mode_name(&self) -> String {
        if let Some(tip_frame_params) = self.tip_frame_params.as_ref() {
            let tip_mode = tip_frame_params.tip_mode;
            if let Ok(name) = self.enum_lookup(ProtoEnumMapping::TipMode, tip_mode) {
                return name;
            }
        }
        "UNKNOWN".into()
    }

    pub fn subsampling_x(&self) -> u8 {
        self.frame_params
            .as_ref()
            .map_or(0, |frame_params| frame_params.subsampling_x as u8)
    }

    pub fn subsampling_y(&self) -> u8 {
        self.frame_params
            .as_ref()
            .map_or(0, |frame_params| frame_params.subsampling_y as u8)
    }
}
