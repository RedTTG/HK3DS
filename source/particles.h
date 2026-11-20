// Particle systems (Unity's Shuriken, the parts Hollow Knight uses): the area particles that follow the camera (dust,
// motes, spores, drips, ash: SceneParticlesController, one set per map zone), the room's own scenery emitters, and
// emitters on objects. Budgeted: a New 3DS keeps up to ~420 particles alive, an Old 3DS ~160; rooms that want more
// get every emitter scaled down evenly instead of some going missing.
#pragma once
#include <stdbool.h>

struct UObj;
void particles_init(void);                    // once: the area particle sets (romfs/rooms/_zones.ptc)
void particles_load(const char *room, const char *zoneName);   // room scenery emitters + the zone's area set
void particles_unload(void);
void particles_update(float dt, float camx, float camy);
void particles_draw_range(float camx, float camy, float flo, float fhi);   // emitters whose parallax F is in [flo, fhi)
void particles_draw_over(float camx, float camy, int slo, int shi);       // emitters in sorting layers [slo, shi), any depth
int  particles_alive(void);
// objects: an emitter compiled into the object's components ({"k":"ps", ...}, tools/obj_particles.py); started by
// playOnAwake when the object activates, or by the FSM (PlayParticleEmitter / StopParticleEmitter / SetParticleEmission...)
struct UComp;
void particles_comp_enable(struct UObj *o, struct UComp *c);    // OnEnable (playOnAwake)
void particles_comp_disable(struct UObj *o, struct UComp *c);   // OnDisable: its particles disappear
void particles_obj_play(struct UObj *o, bool on);   // Play / Stop every ps component on o and its children
void particles_obj_emit(struct UObj *o, int count); // Emit(count) on o's own
void particles_obj_emission(struct UObj *o, bool on);
void particles_obj_rate(struct UObj *o, float rate);
void particles_obj_forget(struct UObj *o);          // destroyed: its world-space particles stay, they stop following
