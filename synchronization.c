#include "synchronization.h"
#include <stddef.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>

#define SYNC_MAX_TRACKED_PLAYERS 8
#define MAX_EXTRAPOLATION_MS 100

typedef struct
{
    int player_id;

    RemotePlayerState previous_state;
    RemotePlayerState newest_state;

    Uint32 previous_time;
    Uint32 newest_time;

    unsigned int last_processed_update_id;

    bool active;
    bool has_newest;
    bool has_previous;

} PlayerSnapshotHistory;

static PlayerSnapshotHistory player_histories[
    SYNC_MAX_TRACKED_PLAYERS
] = {0};

static float InterpolateAngle(
    float start_angle,
    float end_angle,
    float alpha)
{
    const float PI =
        3.14159265358979323846f;

    const float TWO_PI =
        6.28318530717958647692f;

    float difference =
        end_angle - start_angle;

    while (difference > PI)
    {
        difference -= TWO_PI;
    }

    while (difference < -PI)
    {
        difference += TWO_PI;
    }

    return start_angle +
           difference * alpha;
}

static PlayerSnapshotHistory *GetPlayerHistory(
    int player_id)
{
    for (int i = 0; i < SYNC_MAX_TRACKED_PLAYERS; i++)
    {
        if (
            player_histories[i].active &&
            player_histories[i].player_id == player_id)
        {
            return &player_histories[i];
        }
    }

    for (int i = 0; i < SYNC_MAX_TRACKED_PLAYERS; i++)
    {
        if (!player_histories[i].active)
        {
            player_histories[i].active = true;
            player_histories[i].player_id = player_id;

            player_histories[i].last_processed_update_id = 0;

            player_histories[i].has_newest = false;
            player_histories[i].has_previous = false;

            return &player_histories[i];
        }
    }

    return NULL;
}

static SynchronizationMode current_mode =
    SYNC_NO_SMOOTHING;

void Synchronization_SetMode(
    SynchronizationMode mode)
{
    current_mode = mode;
}

SynchronizationMode Synchronization_GetMode(void)
{
    return current_mode;
}

int Synchronization_ProcessPlayers(
    const RemotePlayerState *network_states,
    int network_count,
    RemotePlayerState *output_states,
    int max_output_states)
{
    if (
        network_states == NULL ||
        output_states == NULL)
    {
        return 0;
    }

    int count = network_count;

    if (count > max_output_states)
    {
        count = max_output_states;
    }

    Uint32 current_time = SDL_GetTicks();

    for (int i = 0; i < count; i++)
    {
        PlayerSnapshotHistory *history =
            GetPlayerHistory(
                network_states[i].player_id);

        if (history == NULL)
        {
            continue;
        }

        if (!history->has_newest)
        {
            history->newest_state =
                network_states[i];

            history->newest_time =
                current_time;

            history->last_processed_update_id =
                network_states[i].update_id;

            history->has_newest = true;

            continue;
        }

        bool new_network_update =
            network_states[i].update_id !=
            history->last_processed_update_id;

        if (new_network_update)
        {
            history->previous_state =
                history->newest_state;

            history->previous_time =
                history->newest_time;

            history->newest_state =
                network_states[i];

            history->newest_time =
                current_time;

            history->last_processed_update_id =
                network_states[i].update_id;

            history->has_previous = true;
        }
    }

    if (current_mode == SYNC_NO_SMOOTHING)
    {
        for (int i = 0; i < count; i++)
        {
            output_states[i] =
                network_states[i];
        }

        return count;
    }

    if (current_mode == SYNC_INTERPOLATION)
    {
        for (int i = 0; i < count; i++)
        {
            PlayerSnapshotHistory *history =
                GetPlayerHistory(
                    network_states[i].player_id);

            if (history == NULL)
            {
                output_states[i] =
                    network_states[i];

                continue;
            }

            if (
                !history->has_newest ||
                !history->has_previous)
            {
                output_states[i] =
                    network_states[i];

                continue;
            }

            Uint32 snapshot_interval =
                history->newest_time -
                history->previous_time;

            if (snapshot_interval == 0)
            {
                output_states[i] =
                    history->newest_state;

                continue;
            }

            Uint32 time_since_newest =
                current_time -
                history->newest_time;

            float alpha =
                (float)time_since_newest /
                (float)snapshot_interval;

            if (alpha > 1.0f)
            {
                alpha = 1.0f;
            }

            if (alpha < 0.0f)
            {
                alpha = 0.0f;
            }

            output_states[i] =
                history->newest_state;

            output_states[i].x =
                history->previous_state.x +
                (history->newest_state.x -
                 history->previous_state.x) *
                    alpha;

            output_states[i].y =
                history->previous_state.y +
                (history->newest_state.y -
                 history->previous_state.y) *
                    alpha;

            output_states[i].angle =
                InterpolateAngle(
                    history->previous_state.angle,
                    history->newest_state.angle,
                    alpha);
        }

        return count;
    }


    if (current_mode == SYNC_EXTRAPOLATION)
    {
        for (int i = 0; i < count; i++)
        {
            PlayerSnapshotHistory *history =
                GetPlayerHistory(
                    network_states[i].player_id);

            if (
                history == NULL ||
                !history->has_newest ||
                !history->has_previous)
            {
                output_states[i] =
                    network_states[i];

                continue;
            }

            Uint32 snapshot_interval =
                history->newest_time -
                history->previous_time;

            if (snapshot_interval == 0)
            {
                output_states[i] =
                    history->newest_state;

                continue;
            }

            float velocity_x =
                (history->newest_state.x -
                 history->previous_state.x) /
                (float)snapshot_interval;

            float velocity_y =
                (history->newest_state.y -
                 history->previous_state.y) /
                (float)snapshot_interval;

            const float PI =
                3.14159265358979323846f;

            const float TWO_PI =
                6.28318530717958647692f;

            float angle_difference =
                history->newest_state.angle -
                history->previous_state.angle;

            while (angle_difference > PI)
            {
                angle_difference -= TWO_PI;
            }

            while (angle_difference < -PI)
            {
                angle_difference += TWO_PI;
            }

            float angular_velocity =
                angle_difference /
                (float)snapshot_interval;

            Uint32 prediction_time =
                current_time -
                history->newest_time;

            if (prediction_time > MAX_EXTRAPOLATION_MS)
            {
                prediction_time =
                    MAX_EXTRAPOLATION_MS;
            }

            output_states[i] =
                history->newest_state;

            output_states[i].x =
                history->newest_state.x +
                velocity_x *
                    (float)prediction_time;

            output_states[i].y =
                history->newest_state.y +
                velocity_y *
                    (float)prediction_time;

            output_states[i].angle =
                history->newest_state.angle +
                angular_velocity *
                    (float)prediction_time;

            printf(
                "[EXTRAP] prev=(%.1f, %.1f) newest=(%.1f, %.1f) predicted=(%.1f, %.1f) prediction_time=%u ms\n",
                history->previous_state.x,
                history->previous_state.y,
                history->newest_state.x,
                history->newest_state.y,
                output_states[i].x,
                output_states[i].y,
                prediction_time);
        }

        return count;
    }

    return 0;
}
