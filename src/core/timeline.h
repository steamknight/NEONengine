#ifndef __TIMELINE_H__INCLUDED__
#define __TIMELINE_H__INCLUDED__

#include "types.h"

typedef enum
{
    TIMELINE_IMAGE,
    TIMELINE_TEXT,
} TimelineType;

typedef enum
{
    ANIMATION_PLAY_ONCE,
    ANIMATION_LOOP,
} AnimationType;

typedef enum
{
    INTERPOLATE_LINEAR,
    INTERPOLATE_DISCRETE,
} InterpolationType;

typedef enum
{
    PROP_POSITION,
    PROP_IMAGE_ID,
    PROP_SCRIPT_ID,
} KeyframeProperty;

typedef struct _Timeline
{
    TimelineType type;
    AnimationType animationType;
    InterpolationType interpolation;
    KeyframeProperty property;
    Range keyFrames;
    UWORD uwId;
} Timeline;

typedef struct _Keyframe
{
    ULONG uwTime;
    union
    {
        tUwCoordYX uwPosition;
        UWORD uwImage;
        UWORD uwScriptId;
    };
} Keyframe;

#endif // __TIMELINE_H__INCLUDED__