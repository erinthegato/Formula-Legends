// Race extras on screen: the pit lane (wall, garages, crews, fuel rig), parts that come off the cars,
// smoke / spray / carbon shards, the safety car, night lighting, rain and lightning, TV cameras,
// and the race-control part of the HUD (flags, messages, tyres / fuel / damage).
#include "render.h"
extern const float FRONT_W[ERA_COUNT];     // (render.cpp) the front tyres are narrower
#include "clip.h"
#include <cstdio>
#include <cstring>
#include <vector>

static const float ASPECT = (float)SCREEN_W / SCREEN_H;

// ------------------------------------------------------------------ cars come apart
// Each car model is re-ordered at load time into body / nose / rear wing triangles so the nose and
// wing can be left off (and drawn flying through the air) once they break.
void Renderer::splitCar(int idx) {
    Model& m = carModel[idx];
    for (int li = 0; li < m.lodCount && li < 4; li++) {
        ModelLod& lod = m.lod[li];
        ModelVertex* v = (ModelVertex*)lod.verts;
        int n = (lod.count - lod.glass) / 3;      // the glass stays where it is, at the end
        const float k = 1.0f / 32768.0f;
        float zBot = m.offset[2] - m.scale[2], zTop = m.offset[2] + m.scale[2];
        std::vector<ModelVertex> body, nose, wing;
        body.reserve(lod.count);
        V3 nc(0, 0, 0), wc(0, 0, 0);
        for (int t = 0; t < n; t++) {
            V3 c(0, 0, 0);
            for (int j = 0; j < 3; j++) {
                const ModelVertex& q = v[t * 3 + j];
                c += V3(q.x * k * m.scale[0] + m.offset[0], q.y * k * m.scale[1] + m.offset[1], q.z * k * m.scale[2] + m.offset[2]);
            }
            c = c * (1.0f / 3);
            auto& dst = c.x > m.offset[0] + m.scale[0] * 0.78f ? nose
                      : (c.x < m.offset[0] - m.scale[0] * 0.70f && c.z > zBot + (zTop - zBot) * 0.55f) ? wing : body;
            for (int j = 0; j < 3; j++) dst.push_back(v[t * 3 + j]);
            if (&dst == &nose) nc += c; else if (&dst == &wing) wc += c;
        }
        PartSplit& ps = split[idx][li];
        ps.body = (int)body.size();
        ps.noseFirst = ps.body; ps.noseCount = (int)nose.size();
        ps.wingFirst = ps.noseFirst + ps.noseCount; ps.wingCount = (int)wing.size();
        ps.glassFirst = lod.count - lod.glass; ps.glassCount = lod.glass;
        ps.noseC = ps.noseCount ? nc * (3.0f / ps.noseCount) : V3(m.scale[0], 0, 0.4f);
        ps.wingC = ps.wingCount ? wc * (3.0f / ps.wingCount) : V3(-m.scale[0], 0, 1.0f);
        memcpy(v, body.data(), body.size() * sizeof(ModelVertex));
        if (!nose.empty()) memcpy(v + ps.noseFirst, nose.data(), nose.size() * sizeof(ModelVertex));
        if (!wing.empty()) memcpy(v + ps.wingFirst, wing.data(), wing.size() * sizeof(ModelVertex));
        gfx::flushCache(v, lod.count * (int)sizeof(ModelVertex));
    }
}

// ------------------------------------------------------------------ pit lane
static void addBox(std::vector<WorldVertex>& o, const V3& a, const V3& b, const V3& c, const V3& d, u32 col) {
    WorldVertex A{0, 0, col, a.x, a.y, a.z}, B{0, 0, col, b.x, b.y, b.z}, C{0, 0, col, c.x, c.y, c.z}, D{0, 0, col, d.x, d.y, d.z};
    o.push_back(A); o.push_back(B); o.push_back(C); o.push_back(A); o.push_back(C); o.push_back(D);
}

void Renderer::buildPit(const Race& r) {
    pitVerts.clear();
    const Track& t = r.track;
    pitOrigin = t.world(r.pitIn, r.laneD);
    float hw = t.pts[0].hw;
    float wallD = hw + 2.3f, laneOut = r.boxD + 3.0f, garage = r.boxD + 3.6f;
    auto P = [&](float rel, float d, float z) { V3 p = t.world(r.pitIn + rel, d, z); return p - pitOrigin; };
    float wallFrom = r.pitRel(r.pitWallFrom), wallTo = r.pitRel(r.pitWallTo);
    const float STEP = 10;
    for (float a = -20; a < PIT_LANE_LEN + 20; a += STEP) {
        float b = a + STEP;
        // lane surface (narrower at the entry / exit), white line along the wall
        float in0 = a < wallFrom ? hw + 0.3f : wallD + 0.3f, in1 = b < wallFrom ? hw + 0.3f : wallD + 0.3f;
        if (a > wallTo) in0 = hw + 0.3f;
        if (b > wallTo) in1 = hw + 0.3f;
        addBox(pitVerts, P(a, in0, 0.07f), P(a, laneOut, 0.07f), P(b, laneOut, 0.07f), P(b, in1, 0.07f), RGBA(74, 76, 80));
        // the fast lane / working lane line
        addBox(pitVerts, P(a, r.laneD + 2.2f, 0.09f), P(a, r.laneD + 2.4f, 0.09f), P(b, r.laneD + 2.4f, 0.09f), P(b, r.laneD + 2.2f, 0.09f), RGBA(230, 230, 230));
        if (a >= wallFrom && b <= wallTo) {
            // pit wall: road side, top, lane side (painted in blocks)
            u32 c1 = ((int)(a / STEP) & 1) ? RGBA(205, 205, 200) : RGBA(190, 40, 40);
            addBox(pitVerts, P(a, wallD - 0.25f, 0), P(b, wallD - 0.25f, 0), P(b, wallD - 0.25f, 1.1f), P(a, wallD - 0.25f, 1.1f), c1);
            addBox(pitVerts, P(a, wallD - 0.25f, 1.1f), P(b, wallD - 0.25f, 1.1f), P(b, wallD + 0.25f, 1.1f), P(a, wallD + 0.25f, 1.1f), RGBA(225, 225, 220));
            addBox(pitVerts, P(a, wallD + 0.25f, 1.1f), P(b, wallD + 0.25f, 1.1f), P(b, wallD + 0.25f, 0), P(a, wallD + 0.25f, 0), RGBA(160, 160, 158));
            // debris fence on top
            addBox(pitVerts, P(a, wallD, 1.1f), P(b, wallD, 1.1f), P(b, wallD, 3.4f), P(a, wallD, 3.4f), RGBA(120, 130, 140, 255));
        }
    }
    // garages: one 16 m box per team of this era (+ the career team), door in the team colour
    int era = r.era;
    for (int team = 0; team <= TEAMS_PER_ERA; team++) {
        float a = PIT_BOX0 + team * 16.0f - 7.5f, b = a + 15.0f;
        const CarDef& cd = team < TEAMS_PER_ERA ? CARS[eraFirstCar(era) + team] : CARS[careerCarIndex(era)];
        u32 door = cd.colour | 0xFF000000u;
        // open garage: team-colour lintel over the door, floor, side and back walls, ceiling
        addBox(pitVerts, P(a, garage, 3.6f), P(b, garage, 3.6f), P(b, garage, 4.6f), P(a, garage, 4.6f), door);
        addBox(pitVerts, P(a, garage, 0.03f), P(b, garage, 0.03f), P(b, garage + 12, 0.03f), P(a, garage + 12, 0.03f), RGBA(120, 124, 132));
        addBox(pitVerts, P(a, garage, 0), P(a, garage + 12, 0), P(a, garage + 12, 4.6f), P(a, garage, 4.6f), RGBA(200, 204, 212));
        addBox(pitVerts, P(a, garage + 11.9f, 0), P(b, garage + 11.9f, 0), P(b, garage + 11.9f, 4.6f), P(a, garage + 11.9f, 4.6f), RGBA(176, 180, 190));
        addBox(pitVerts, P(a + 1, garage + 11.85f, 1.2f), P(b - 1, garage + 11.85f, 1.2f), P(b - 1, garage + 11.85f, 1.7f), P(a + 1, garage + 11.85f, 1.7f), door);
        addBox(pitVerts, P(a, garage, 4.6f), P(b, garage, 4.6f), P(b, garage + 12, 4.6f), P(a, garage + 12, 4.6f), RGBA(150, 152, 158));
        addBox(pitVerts, P(a, garage + 12, 0), P(b + 1, garage + 12, 0), P(b + 1, garage + 12, 4.6f), P(a, garage + 12, 4.6f), RGBA(120, 122, 128));
        addBox(pitVerts, P(b, garage, 0), P(b + 1, garage, 0), P(b + 1, garage, 4.6f), P(b, garage, 4.6f), RGBA(230, 230, 232));
        // painted box on the lane
        addBox(pitVerts, P(a + 2, r.boxD - 2.2f, 0.09f), P(a + 2.3f, r.boxD - 2.2f, 0.09f), P(a + 2.3f, r.boxD + 2.2f, 0.09f), P(a + 2, r.boxD + 2.2f, 0.09f), RGBA(250, 210, 40));
        addBox(pitVerts, P(b - 2.3f, r.boxD - 2.2f, 0.09f), P(b - 2, r.boxD - 2.2f, 0.09f), P(b - 2, r.boxD + 2.2f, 0.09f), P(b - 2.3f, r.boxD + 2.2f, 0.09f), RGBA(250, 210, 40));
    }
    // pit building above the garages
    for (float a = PIT_BOX0 - 20; a < PIT_BOX0 + 16 * (TEAMS_PER_ERA + 1); a += 20)
        addBox(pitVerts, P(a, garage + 1, 4.6f), P(a + 20, garage + 1, 4.6f), P(a + 20, garage + 1, 11), P(a, garage + 1, 11), RGBA(70, 90, 120));
    // start-light gantry over the track just past the line (boxes where the Blender gantry is missing)
    if (!prop[PROP_GANTRY].lodCount) {
        float g = PIT_IN_BEFORE + 8;      // metres after the pit entry = 8 m past the line
        float hw0 = t.pts[t.index(8)].hw;
        for (int sd = -1; sd <= 1; sd += 2) {
            float d0 = sd * (hw0 + 0.8f);
            addBox(pitVerts, P(g, d0 - 0.15f, 0), P(g, d0 + 0.15f, 0), P(g, d0 + 0.15f, 6.4f), P(g, d0 - 0.15f, 6.4f), RGBA(70, 74, 82));
            addBox(pitVerts, P(g - 0.3f, d0, 0), P(g + 0.3f, d0, 0), P(g + 0.3f, d0, 6.4f), P(g - 0.3f, d0, 6.4f), RGBA(60, 64, 72));
        }
        addBox(pitVerts, P(g, -hw0 - 0.8f, 5.3f), P(g, hw0 + 0.8f, 5.3f), P(g, hw0 + 0.8f, 6.4f), P(g, -hw0 - 0.8f, 6.4f), RGBA(40, 42, 50));
    }
    gfx::flushCache(pitVerts.data(), (int)pitVerts.size() * (int)sizeof(WorldVertex));
    // the safety car: each era's own (loadEraParts); the Ashcombe Vanguard road car only where that's missing
    scModel = -1;
    for (int i = 0; i < CAR_COUNT; i++) if (!strcmp(CARS[i].id, "ashcombevanguard")) scModel = i;
    int scE = r.cars.empty() ? 0 : r.cars[0].def->era % ERA_COUNT;
    loadEraParts(scE);
    if (scModel >= 0 && !safetyCar[scE].lodCount) loadCar(scModel);
}

void Renderer::drawPit(const Race& r, const Camera& cam) {
    if (pitVerts.empty()) return;
    V3 centre = r.track.world(r.pitIn + PIT_LANE_LEN * 0.5f, r.boxD);
    float d = len(centre - cam.eye);
    if (d > 900) return;
    gfx::bindTexture(nullptr);
    M4 m = M4::translate(pitOrigin - cam.eye);
    if (d < 400) clip::drawWorld(pitVerts.data(), (int)pitVerts.size(), m);
    else gfx::drawWorld(pitVerts.data(), (int)pitVerts.size(), PRIM_TRIANGLES, m);
    // the start lights on the gantry (red one by one, then green)
    if (r.session == SES_RACE && r.time < 6 && !r.formation) {
        bool waiting = false;
        for (const Car& o : r.cars) if (o.startDelay > 0) waiting = true;
        V3 gp, gt, gl; float ghw;
        r.track.frame(8.0f, gp, gt, gl, ghw);
        // the Blender gantry is scaled to the track (Track::gantryScale): its lamp panel is 4.6-5.32 m up at full size
        float sc = prop[PROP_GANTRY].lodCount ? r.track.gantryScale : 1.0f;
        V3 at = prop[PROP_GANTRY].lodCount ? gp - gt * (0.28f * sc) + V3(0, 0, 4.96f * sc) : gp - gt * 0.06f + V3(0, 0, 5.85f);
        M4 face = M4::translate(at - cam.eye) * M4::basis(gl * -1.0f, V3(0, 0, 1), gt * -1.0f, V3(0, 0, 0));
        int lit = r.countdown > 0 ? (int)(5 - (r.countdown - 0.5f) / 0.7f) : 0;
        for (int k = 0; k < 5; k++) {
            u32 col = r.countdown > 0 ? (k < lit && !waiting ? RGBA(255, 30, 20) : RGBA(50, 16, 16)) : (r.time < 3.5f ? RGBA(40, 240, 70) : RGBA(16, 50, 20));
            float x = (k - 2) * 1.0f * sc, h = 0.36f * sc;
            quad3D(face, x - h, -h, x + h, h, col);
        }
    }
    // crews at cars stopped in their box -- and waiting, ready, for a car coming down the pit lane
    for (const Car& c : r.cars) {
        bool stopped = c.pit == PIT_STOP;
        float toBox = 1e9f;
        if (!stopped && c.pit == PIT_IN && c.pitPhase != -1 && r.session == SES_RACE)
            toBox = r.pitRel(r.boxS(c)) - r.pitRel(c.s);
        bool ready = !stopped && toBox > -2 && toBox < 150;
        // just released: the crew stay in the box until the car is clear
        bool after = c.pit == PIT_OUT && c.pitPhase == 0 && r.session == SES_RACE &&
                     r.pitRel(c.s) - r.pitRel(r.boxS(c)) < 30 && r.pitRel(c.s) - r.pitRel(r.boxS(c)) > -1;
        if (!stopped && !ready && !after) continue;
        M4 place;
        V3 rel;
        if (stopped) {
            rel = c.pos - cam.eye;
            place = M4::basis(c.fwd, c.left, V3(0, 0, 1), rel - V3(0, 0, c.lift));
        } else {                                       // the box the car is heading for
            float bs = r.boxS(c);
            V3 p, t, l; float hw; r.track.frame(bs, p, t, l, hw);
            rel = r.track.world(bs, r.boxD) - cam.eye;
            place = M4::basis(t, l, V3(0, 0, 1), rel);
        }
        if (dot(rel, rel) > 70.0f * 70.0f) continue;
        float tt = r.time * 3.0f + c.number;
        u32 tm, ts; teamColours(c.def, tm, ts);        // the crew wear the team's colours
        int era = c.def->era % ERA_COUNT;
        if (stopped && c.pitPhase <= -2) {
            // in the garage: the crew round the car; when it goes out, two push at the back and one at each side
            bool pushing = c.pitPhase == -3 && c.pitTimer > 2.6f;
            float hl = c.def->lengthM * 0.5f;
            const float GX[4] = {-hl - 0.5f, -hl - 0.5f, 0.6f, 0.6f}, GY[4] = {0.45f, -0.45f, 1.35f, -1.35f};
            const float IX[4] = {-hl - 1.6f, 1.2f, -0.8f, 2.6f}, IY[4] = {1.6f, 1.9f, -1.9f, -0.4f};
            for (int k = 0; k < 4; k++) {
                float x = pushing ? GX[k] : IX[k], y = pushing ? GY[k] : IY[k];
                float yaw = pushing ? (k < 2 ? 0.0f : (y > 0 ? -1.5708f : 1.5708f)) : atan2f(-y, -x);
                bool animated = poseCount[pushing ? ANIM_PUSH : ANIM_IDLE] > 0;
                float lean = pushing && k < 2 && !animated ? 0.25f : 0;
                M4 p = place * M4::translate(V3(x, y, 0)) * M4::rotZ(yaw) * M4::rotY(lean);
                drawPerson(p, pushing ? ANIM_PUSH : ANIM_IDLE, tt * 0.33f + k * 0.4f, false, tm, ts);
            }
            continue;
        }
        // A Formula 1 crew. At each wheel: the gun man (crouched in close), the man who takes the old
        // wheel off and carries it away, and the man who brings the new one; the front and rear jack men
        // with their jacks; a man steadying each side; and the release: a lollipop man in front of the
        // car (1998) or the light gantry over its nose (2010 on: red while they work, green to go).
        int ph = stopped ? c.pitPhase : (after ? 3 : -9);       // -9: waiting for the car; after it leaves: 3
        bool wheels = ph == 1, done = (stopped || after) && ph >= 2 && !(ph == 2 && c.plan.repair);
        static const float SWAP[ERA_COUNT] = {1.6f, 3.2f, 2.6f, 2.3f};   // as Race::pitStop
        float f = wheels ? clampf(c.pitTimer / SWAP[era], 0, 1) : (done ? 1.0f : 0.0f);
        bool gunReady = ph == -9 || ph == 0;           // crouched at the wheel before the car is up
        float in = wheels || gunReady ? 1.05f : 1.8f;
        const Model& body = carModel[c.def - CARS];
        float xf = body.axleF != 0 ? body.axleF : 1.6f, xr = body.axleR != 0 ? body.axleR : -1.6f;
        float wy = body.axleF != 0 ? body.wheelY : 0.86f;
        float hl = c.def->lengthM * 0.5f;
        const float WX[8] = {xf, xf, xr, xr, hl + 0.7f, -hl - 0.6f, 0.1f, 0.1f};
        const float WY[8] = {1, -1, 1, -1, 0, 0, 1.25f, -1.25f};
        const Model& wm = eraWheel[era].lodCount ? eraWheel[era] : wheel;
        float rad = wm.lodCount ? wm.scale[2] : 0.33f;
        // the jack men keep out of the car's way: the rear one waits beside the box (garage side, away
        // from the fast lane) until the car has stopped, then steps in behind it; the front one -- and
        // the 1998 lollipop man -- pull out to the garage side as the car drops off its jacks to leave
        const float ASIDE = 1.7f;
        float rearIn = stopped ? clampf(c.pitTotal / 0.2f, 0, 1) : (after ? 1.0f : 0.0f);
        float frontOut = after ? 1.0f : (stopped && ph == 3 ? clampf(c.pitTimer / 0.3f, 0, 1) : 0.0f);
        rearIn = rearIn * rearIn * (3 - 2 * rearIn); frontOut = frontOut * frontOut * (3 - 2 * frontOut);
        for (int k = 0; k < 8; k++) {
            float y = (k < 4 ? WY[k] * in : WY[k]) + (wheels && k < 4 ? sinf(tt + k) * 0.05f : 0);
            float yaw = k < 4 || k >= 6 ? (WY[k] > 0 ? -1.57f : 1.57f) : (WX[k] > 0 ? 3.14f : 0);
            float xk = WX[k];
            if (k == 4) { y += frontOut * ASIDE; xk += frontOut * 0.6f; yaw -= frontOut * 1.2f; }   // front: out of the way
            if (k == 5) { y += (1 - rearIn) * ASIDE; xk -= (1 - rearIn) * 0.4f; }                  // rear: waits beside
            bool gun = (wheels || gunReady) && k < 4;
            float squat = gun && !poseCount[ANIM_WHEELGUN] ? 0.7f : 1.0f;
            M4 p = place * M4::translate(V3(xk, y, 0)) * M4::rotZ(yaw) * M4::scale(1, 1, squat);
            // the gun only works while the car is up; waiting, they hold the first pose
            float at = gunReady ? 0.0f : tt * 0.33f + k * 0.3f;
            drawPerson(p, gun ? ANIM_WHEELGUN : ANIM_IDLE, at, false, tm, ts);
            if (k < 4) {
                float sgn = WY[k] > 0 ? 1.0f : -1.0f, sy = sgn * (in + 0.75f);
                bool front = k < 2;
                // wheel off: in by the wheel, then away with the old one once the gun has done its work
                float offAway = clampf((f - 0.45f) / 0.4f, 0, 1);
                V3 offAt(WX[k] - 0.75f - offAway * 1.1f, sgn * (in + 0.75f + offAway * 0.6f), 0);
                // wheel on: waiting with the new one, in with it as the old one comes off
                float onIn = clampf((f - 0.3f) / 0.2f, 0, 1);
                V3 onAt(WX[k] + 0.75f - onIn * 0.25f, sy, 0);
                M4 a = place * M4::translate(offAt) * M4::rotZ(sgn > 0 ? -2.2f : 2.2f);
                M4 b = place * M4::translate(onAt) * M4::rotZ(sgn > 0 ? -1.2f : 1.2f);
                drawPerson(a, ANIM_IDLE, tt * 0.33f + k * 0.5f + 1.0f, false, tm, ts);
                drawPerson(b, ANIM_IDLE, tt * 0.33f + k * 0.7f + 2.0f, false, tm, ts);
                // the wheels they carry: the new one until it goes on, the old one after it comes off
                if (wm.lodCount && stopped && c.plan.tyres && (ph == 0 || ph == 1 || ph == -9) && f < 0.5f) {
                    M4 w = place * M4::translate(onAt + V3(-0.05f, -sgn * 0.42f, rad + 0.18f));
                    if (front) w = w * M4::scale(1, FRONT_W[era], 1);
                    gfx::bindTexture(&wm.lod[1 < wm.lodCount ? 1 : 0].tex);
                    gfx::drawModel(wm.lod[1 < wm.lodCount ? 1 : 0].verts, wm.lod[1 < wm.lodCount ? 1 : 0].count, wm.matrix(w));
                } else if (wm.lodCount && wheels && f >= 0.5f) {
                    M4 w = place * M4::translate(offAt + V3(0.05f, -sgn * 0.42f, rad + 0.18f));
                    if (front) w = w * M4::scale(1, FRONT_W[era], 1);
                    gfx::bindTexture(&wm.lod[1 < wm.lodCount ? 1 : 0].tex);
                    gfx::drawModel(wm.lod[1 < wm.lodCount ? 1 : 0].verts, wm.lod[1 < wm.lodCount ? 1 : 0].count, wm.matrix(w));
                }
                (void)wy;
            }
        }
        if (ph == 0 || ph == -2) {
            M4 p = place * M4::translate(V3(-0.8f, 1.6f, 0)) * M4::rotZ(-1.57f);
            drawPerson(p, ANIM_IDLE, tt * 0.33f + 2.0f, false, tm, ts);
        }
        // the jacks: a low trolley under the nose and the tail, the handle up to the jack man's hands
        {
            u32 steel = RGBA(70, 72, 78), handle = RGBA(30, 30, 34);
            float lift = stopped ? c.lift : 0;
            if (after) lift = 0;
            for (int e = 0; e < 2; e++) {
                // under the nose / the rear crash structure; out with its man when he stands aside
                float out = e == 0 ? frontOut : 1 - rearIn;
                float x0 = e == 0 ? hl - 0.15f + out * 0.9f : -hl + 0.05f - out * 0.9f;
                float y0 = out * ASIDE;
                float jl = out > 0.01f ? 0.0f : lift;                  // pulled out: down on its wheels
                M4 g = place * M4::translate(V3(0, 0, stopped && !after ? c.lift : 0));
                box3D(g, V3(x0 - 0.2f, y0 - 0.2f, 0), V3(x0 + 0.2f, y0 + 0.2f, 0.1f + jl), steel);
                // the handle runs from the trolley up to his hands, 1.1 m out at waist height
                M4 h = g * M4::translate(V3(x0, y0, 0.1f + jl)) * M4::rotZ(e == 0 ? 0.0f : 3.14159f) * M4::rotY(-0.62f);
                box3D(h, V3(0, -0.03f, -0.03f), V3(1.25f, 0.03f, 0.03f), handle);
            }
        }
        // the release
        bool go = done && ph == 3;
        if (era == 0) {
            // 1998: the lollipop man in front of the nose; the board comes up when the car may go
            float hx = hl + 1.4f, hy = 0.35f + frontOut * (ASIDE + 0.6f);     // steps aside with the front jack man
            M4 p = place * M4::translate(V3(hx, hy, 0)) * M4::rotZ(3.14159f);
            drawPerson(p, ANIM_IDLE, tt * 0.33f + 3.0f, false, tm, ts);
            float up = go || after ? 0.75f : 0.0f;
            M4 pole = place * M4::translate(V3(hx - 0.35f, hy - 0.15f, 0));
            box3D(pole, V3(-0.02f, -0.02f, 0.95f + up), V3(0.02f, 0.02f, 1.55f + up), RGBA(30, 30, 30));
            box3D(pole, V3(-0.03f, -0.22f, 1.5f + up), V3(0.0f, 0.22f, 1.85f + up), go ? RGBA(240, 240, 235) : RGBA(210, 25, 20));
        } else {
            // 2010 on: a light gantry from the garage side over the nose
            float gx = xf + 0.6f;
            u32 frame = RGBA(55, 57, 62);
            box3D(place, V3(gx - 0.06f, 2.3f, 0), V3(gx + 0.06f, 2.42f, 3.1f), frame);
            box3D(place, V3(gx - 0.06f, -0.2f, 2.98f), V3(gx + 0.06f, 2.42f, 3.1f), frame);
            box3D(place, V3(gx - 0.12f, -0.18f, 2.62f), V3(gx + 0.06f, 0.18f, 2.98f), RGBA(18, 18, 20));
            M4 face = place * M4::translate(V3(gx - 0.13f, 0, 2.8f)) * M4::basis(V3(0, -1, 0), V3(0, 0, 1), V3(-1, 0, 0), V3(0, 0, 0));
            u32 lamp = go ? RGBA(40, 240, 70) : RGBA(255, 30, 20);
            quad3D(face, -0.12f, -0.1f, 0.12f, 0.12f, lamp);
        }
        if (fuelRig.lodCount && r.ep().refuel && stopped) {    // refuelling only in the 1990s
            gfx::bindTexture(&fuelRig.lod[0].tex);
            M4 p = place * M4::translate(V3(-0.8f, 4.0f, 0)) * M4::rotZ(1.57f);
            gfx::drawModel(fuelRig.lod[0].verts, fuelRig.lod[0].count, fuelRig.matrix(p));
        }
    }
}

// ---- life at the trackside: marshal light panels and fans waving flags
void Renderer::drawTrackLife(const Race& r, const Camera& cam) {
    // The light panels (tools/blender/trackside.py flagpanel: the lamp face is 0.64 x 0.44 m, 2.18-2.62 m
    // up, facing the track). Flashing yellow round an incident (its slow zone, or one coming up within
    // 150 m), all round the lap under the VSC or the safety car; blue when a car here is being caught by
    // a faster one. Dark otherwise.
    bool flash = fmodf(r.time * 2.0f, 1.0f) < 0.6f;
    bool everywhere = r.sc.state || r.fcy > 0;
    for (const PropInst& p : r.track.props) {
        if (p.model != PROP_FLAGPANEL) continue;
        V3 rel = p.p - cam.eye;
        if (dot(rel, rel) > 260.0f * 260.0f || dot(rel, cam.fwd) < -2) continue;
        u32 col = 0;
        if (everywhere || r.zoneAt(p.s) || r.zoneAt(r.track.wrap(p.s + 150))) col = flash ? RGBA(255, 210, 20) : 0;
        if (!col && !everywhere) {
            for (const Car& c : r.cars) {
                if (c.blue <= 0) continue;
                float ahead = r.track.wrap(p.s - c.s);
                if (ahead < 90 || ahead > r.track.length - 10) { col = flash ? RGBA(40, 120, 255) : 0; break; }
            }
        }
        if (!col) continue;
        M4 face = M4::translate(rel) * M4::rotZ(p.yaw) * M4::translate(V3(0.225f, 0, 2.4f)) *
                  M4::basis(V3(0, 1, 0), V3(0, 0, 1), V3(1, 0, 0), V3(0, 0, 0));
        quad3D(face, -0.31f, -0.21f, 0.31f, 0.21f, col);
    }
    // Fans' flags: a few in each stand within 160 m, in the colours of the teams racing, waving.
    if (!perf.effects || r.cars.empty()) return;
    const int MAXF = 160;
    WorldVertex* v = (WorldVertex*)gfx::frameAlloc(MAXF * 12 * sizeof(WorldVertex));
    if (!v) return;
    int o = 0, nf = 0, nc = (int)r.cars.size();
    for (const PropInst& p : r.track.props) {
        bool stand = p.model == PROP_GRANDSTAND, bl = p.model == PROP_BLEACHER;
        if (!stand && !bl) continue;
        V3 rel = p.p - cam.eye;
        if (dot(rel, rel) > 160.0f * 160.0f || dot(rel, cam.fwd) < -16) continue;
        M4 m = M4::translate(rel) * M4::rotZ(p.yaw) * M4::scale(p.scale, p.scale, p.scale);
        int count = stand ? 14 : 5;
        u32 seed = (u32)(p.p.x * 13.0f) * 2654435761u + (u32)(p.p.y * 7.0f);
        for (int k = 0; k < count && nf < MAXF; k++) {
            seed = seed * 1664525u + 1013904223u;
            int row = stand ? (int)(seed >> 8) % 8 : (int)(seed >> 8) % 5;
            float yy = ((seed >> 16) & 1023) / 1023.0f * 2 - 1;
            // where a fan sits: the front of his row, his head's height (as the Blender stands)
            float x = stand ? 5.0f - row * 0.95f - 0.35f : -row * 0.9f - 0.45f;
            float z = stand ? 1.1f + row * 0.72f + 0.55f : 0.45f + row * 0.55f + 0.5f;
            float y = yy * (stand ? 14.5f : 4.4f);
            float ph = (seed & 255) / 40.0f, t = r.time;
            float sway = sinf(t * 3.1f + ph) * 0.18f;                // the arm waves it from side to side
            float flap = sinf(t * 9.0f + ph * 2) * 0.12f;            // the cloth flutters
            u32 col = r.cars[(k * 7 + row) % nc].def->colour | 0xFF000000u;
            V3 a(x + 0.05f, y + sway, z + 0.35f), b(x + 0.05f, y + sway, z + 0.75f);       // along the pole
            V3 c(x + 0.05f + flap, y + sway + 0.55f, z + 0.78f), d(x + 0.05f + flap, y + sway + 0.55f, z + 0.38f);
            V3 q[4] = {a, d, c, b};
            WorldVertex w[4];
            for (int i = 0; i < 4; i++) {
                V3 wp = m.point(q[i]);
                w[i] = WorldVertex{0, 0, i >= 1 && i <= 2 ? col : (col & 0xFFE0E0E0u), wp.x, wp.y, wp.z};
            }
            // both sides of the cloth
            v[o++] = w[0]; v[o++] = w[1]; v[o++] = w[2]; v[o++] = w[0]; v[o++] = w[2]; v[o++] = w[3];
            v[o++] = w[0]; v[o++] = w[2]; v[o++] = w[1]; v[o++] = w[0]; v[o++] = w[3]; v[o++] = w[2];
            nf++;
        }
    }
    if (!o) return;
    gfx::flushCache(v, o * (int)sizeof(WorldVertex));
    gfx::bindTexture(nullptr);
    gfx::drawWorld(v, o, PRIM_TRIANGLES, M4::identity());
}

// a solid box (place-space corners lo..hi), flat-coloured, lit from above, in one draw
void Renderer::box3D(const M4& place, const V3& lo, const V3& hi, u32 col) {
    WorldVertex* v = (WorldVertex*)gfx::frameAlloc(36 * sizeof(WorldVertex));
    if (!v) return;
    auto shade = [&](float k) {
        int r_ = (int)((col & 0xFF) * k), g_ = (int)(((col >> 8) & 0xFF) * k), b_ = (int)(((col >> 16) & 0xFF) * k);
        return RGBA(r_ > 255 ? 255 : r_, g_ > 255 ? 255 : g_, b_ > 255 ? 255 : b_);
    };
    V3 c[8];
    for (int i = 0; i < 8; i++) c[i] = V3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z);
    // faces (counter-clockwise from outside) and their light: +Z, -Z, +X, -X, +Y, -Y
    static const int F[6][4] = {{4, 5, 7, 6}, {0, 2, 3, 1}, {1, 3, 7, 5}, {0, 4, 6, 2}, {2, 6, 7, 3}, {0, 1, 5, 4}};
    static const float L[6] = {1.15f, 0.45f, 0.9f, 0.75f, 0.85f, 0.7f};
    int o = 0;
    for (int f = 0; f < 6; f++) {
        u32 k = shade(L[f]);
        const int* q = F[f];
        int idx[6] = {q[0], q[1], q[2], q[0], q[2], q[3]};
        for (int i : idx) { v[o++] = WorldVertex{0, 0, k, c[i].x, c[i].y, c[i].z}; }
    }
    gfx::flushCache(v, 36 * sizeof(WorldVertex));
    gfx::bindTexture(nullptr);
    gfx::drawWorld(v, 36, PRIM_TRIANGLES, place);
}

// ------------------------------------------------------------------ debris, particles, safety car
void Renderer::drawEffects(const Race& r, const Camera& cam, bool ownLights) {
    // the safety car: the era's own (made in Blender), else the old road car
    int scEra = r.cars.empty() ? 0 : r.cars[0].def->era % ERA_COUNT;
    const Model& scm = safetyCar[scEra];
    float scRoof = 1.35f;
    if (r.sc.state && scm.lodCount) {
        V3 rel = r.sc.pos - cam.eye;
        float d = len(rel);
        int li = d < 30 ? 0 : (d < 80 ? 1 : 2);
        if (li >= scm.lodCount) li = scm.lodCount - 1;
        M4 place = M4::basis(r.sc.fwd, r.sc.left, r.sc.up, rel);
        gfx::bindTexture(&scm.lod[li].tex);
        if (d < 9) clip::drawModel(scm.lod[li].verts, scm.lod[li].count, scm.matrix(place));
        else gfx::drawModel(scm.lod[li].verts, scm.lod[li].count, scm.matrix(place));
        scRoof = scm.offset[2] + scm.scale[2] - 0.04f;       // its light bar is the top of the model
    } else if (r.sc.state && scModel >= 0 && carLoaded[scModel]) {
        Car tmp;
        tmp.def = &CARS[scModel];
        tmp.pos = r.sc.pos; tmp.fwd = r.sc.fwd; tmp.left = r.sc.left; tmp.up = r.sc.up;
        float tanV = tanf(cam.fov * 3.14159f / 360.0f), tanH = tanV * ASPECT;
        drawCar(tmp, cam.eye, cam.fwd, tanH, tanV);
    }
    // flying noses and wings
    for (const Debris& d : r.debris) {
        const Car& c = r.cars[d.carIdx];
        int idx = (int)(c.def - CARS);
        if (!carLoaded[idx]) continue;
        V3 rel = d.p - cam.eye;
        if (dot(rel, rel) > 200.0f * 200.0f) continue;
        const Model& m = carModel[idx];
        int li = m.lodCount > 1 ? 1 : 0;
        const PartSplit& ps = split[idx][li];
        int first = d.part == DM_NOSE ? ps.noseFirst : ps.wingFirst, count = d.part == DM_NOSE ? ps.noseCount : ps.wingCount;
        if (!count) continue;
        V3 centre = d.part == DM_NOSE ? ps.noseC : ps.wingC;
        M4 place = M4::translate(rel) * M4::rotZ(d.yaw) * M4::rotY(d.pitch) * M4::translate(centre * -1.0f);
        gfx::bindTexture(&m.lod[li].tex);
        gfx::drawModel(m.lod[li].verts + first, count, m.matrix(place));
    }
    // smoke, spray, shards, and lights (safety car beacons, night head / tail lights)
    float night = r.nightLevel();
    int maxQ = (int)r.parts.size() + 6 * (int)r.cars.size() + 8;   // lamps (4) and brake discs (2) per car
    WorldVertex* v = (WorldVertex*)gfx::frameAlloc(maxQ * 6 * sizeof(WorldVertex));
    if (!v) return;
    int o = 0;
    auto quad = [&](const V3& rel, float s, u32 col) {
        if (o / 6 >= maxQ) return;
        V3 a = camRight * s, b = camUp * s;
        V3 p0 = rel - a - b, p1 = rel + a - b, p2 = rel + a + b, p3 = rel - a + b;
        WorldVertex q0{0, 1, col, p0.x, p0.y, p0.z}, q1{1, 1, col, p1.x, p1.y, p1.z}, q2{1, 0, col, p2.x, p2.y, p2.z}, q3{0, 0, col, p3.x, p3.y, p3.z};
        v[o++] = q0; v[o++] = q1; v[o++] = q2; v[o++] = q0; v[o++] = q2; v[o++] = q3;
    };
    int pi = 0;
    for (const Particle& p : r.parts) {
        if (!perf.effects && p.kind != 2 && (pi++ & 1)) continue;      // reduced effects: every other puff
        V3 rel = p.p - cam.eye;
        if (dot(rel, cam.fwd) < 0.5f || dot(rel, rel) > 140.0f * 140.0f) continue;
        u32 col = p.color;
        if (p.kind != 2) {
            float a = (float)(col >> 24) * clampf(p.life, 0, 1);
            col = (col & 0xFFFFFFu) | ((u32)a << 24);
        }
        quad(rel, p.size * (p.kind == 2 ? 1.0f : 1.6f), col);
    }
    // brake discs: hard braking from speed heats them; they glow orange through the wheel spokes
    {
        float hdt = heatLastT < 0 ? 0 : clampf(r.time - heatLastT, 0, 0.1f);
        if (ownLights) heatLastT = r.time;           // (the mirror pass doesn't advance them)
        float night = r.nightLevel();
        for (int i = 0; i < (int)r.cars.size() && i < 64; i++) {
            const Car& c = r.cars[i];
            float& h = brakeHeat[i];
            if (ownLights) h = clampf(h + (c.brake * fminf(c.v, 85.0f) / 85.0f * 0.9f - 0.22f) * hdt, 0, 1);
            if (h < 0.15f) continue;
            V3 rel = c.pos - cam.eye;
            if (dot(rel, rel) > 60.0f * 60.0f || dot(rel, cam.fwd) < 0) continue;
            float half = c.def->lengthM * 0.5f;
            int a = (int)(255 * clampf((h - 0.15f) * 1.6f, 0, 1) * (0.35f + 0.65f * night));
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                V3 w = rel + c.fwd * (half * 0.58f) + c.left * (0.86f * sgn) + c.up * 0.36f;
                quad(w, 0.17f, RGBA(255, 110 + (int)(90 * h), 30, a));
            }
        }
    }
    if (r.sc.state) {
        bool flash = ((int)(r.time * 6)) & 1;
        V3 roof = r.sc.pos + r.sc.up * scRoof - cam.eye;
        quad(roof + r.sc.left * 0.45f, 0.28f, flash ? RGBA(255, 170, 20, 240) : RGBA(255, 120, 0, 90));
        quad(roof - r.sc.left * 0.45f, 0.28f, flash ? RGBA(255, 120, 0, 90) : RGBA(255, 170, 20, 240));
    }
    // Formula 1 cars carry no headlights (night races are floodlit): one red light in the middle of
    // the rear crash structure, lit in the modes its era's rules call for (rearLightMode)
    for (const Car& c : r.cars) {
        if (!ownLights && c.player) continue;           // from the seat we don't see our own light
        int mode = rearLightMode(r, c);
        if (mode == RL_OFF) continue;
        V3 rel = c.pos - cam.eye;
        float d2 = dot(rel, rel);
        if (d2 > 300.0f * 300.0f) continue;
        float back = dot(rel, c.fwd) / sqrtf(fmaxf(d2, 0.01f));   // 1: straight behind the car
        if (back < -0.2f) continue;                     // it shines backwards only
        if (!rearLightLit(mode, r.time + c.number * 0.173f)) continue;
        const V3& at = REAR_LIGHT[c.def->era % ERA_COUNT];
        V3 p = rel + c.fwd * at.x + c.left * at.y + c.up * at.z;
        float glow = (0.10f + 0.12f * fmaxf(night, r.rain)) * (0.55f + 0.45f * clampf(back, 0, 1));
        quad(p, glow, RGBA(255, 28, 18, (int)(235 * clampf(back + 0.2f, 0, 1))));
    }
    if (!o) return;
    gfx::flushCache(v, o * (int)sizeof(WorldVertex));
    gfx::bindTexture(&puff);
    gfx::setBlend(true); gfx::setDepthWrite(false);
    gfx::drawWorld(v, o, PRIM_TRIANGLES, M4::identity());
    gfx::setDepthWrite(true); gfx::setBlend(false);
}

// ------------------------------------------------------------------ sky, night, rain, lightning
u32 Renderer::skyColour(const Race& r) const {
    float n = r.sunLevel(), w = r.rain;          // the sky follows the sun; the floodlit circuit does not
    float cr = r.track.desert ? 150 : 120, cg = r.track.desert ? 185 : 170, cb = r.track.desert ? 225 : 230;
    // dusk tint
    float h = r.clockHour();
    float dusk = (h > 18.5f && h < 21) ? 1 - fabsf(h - 19.8f) / 1.3f : ((h > 5.5f && h < 7.8f) ? 1 - fabsf(h - 6.6f) / 1.1f : 0);
    dusk = clampf(dusk, 0, 1);
    cr += dusk * 90; cg += dusk * 10; cb -= dusk * 60;
    cr = cr * (1 - w * 0.45f) + 130 * w * 0.45f; cg = cg * (1 - w * 0.45f) + 135 * w * 0.45f; cb = cb * (1 - w * 0.45f) + 140 * w * 0.45f;
    float k = 1 - n * 0.9f;
    float lf = r.lightning;
    return RGBA((int)clampf(cr * k + lf * 200, 0, 255), (int)clampf(cg * k + lf * 200, 0, 255), (int)clampf(cb * k + 12 * n + lf * 200, 0, 255));
}

// ---- the F1 rear light. Position: the tail of each era's rear crash structure (tools/blender: the
// RainLight lens). Modes, by the rules of the time:
//   rain     every era: on in the wet (inters / wets fitted, or real rain): steady in 1998, flashing later
//   harvest  hybrid eras (2016, 2025): flashes fast while the MGU-K recovers energy under braking
//   limiter  2016, 2025: flashes slowly while the pit lane speed limiter is on
const V3 Renderer::REAR_LIGHT[ERA_COUNT] = {V3(-1.94f, 0, 0.25f), V3(-2.02f, 0, 0.28f), V3(-1.97f, 0, 0.24f), V3(-2.22f, 0, 0.26f)};

int Renderer::rearLightMode(const Race& r, const Car& c) const {
    int era = c.def->era % ERA_COUNT;
    bool hybrid = era >= 2;
    if (hybrid && c.pit != PIT_NONE && r.inPitLane(c)) return RL_LIMITER;
    if (hybrid && c.pit == PIT_NONE && c.brake > 0.1f && c.v > 8 && c.ers < 0.999f) return RL_HARVEST;
    if (c.tyre == TY_INTER || c.tyre == TY_WET || r.rain > 0.35f) return era == 0 ? RL_RAIN_STEADY : RL_RAIN;
    return RL_OFF;
}

bool Renderer::rearLightLit(int mode, float t) {
    switch (mode) {
        case RL_RAIN_STEADY: return true;
        case RL_RAIN: return fmodf(t * 4.0f, 1.0f) < 0.5f;          // 4 flashes a second
        case RL_HARVEST: return fmodf(t * 8.0f, 1.0f) < 0.45f;      // rapid
        case RL_LIMITER: return fmodf(t * 2.0f, 1.0f) < 0.5f;       // slow
        default: return false;
    }
}

float Renderer::cabinDark(const Race& r) const { return r.nightLevel() * 0.65f; }

void Renderer::drawNight(const Race& r, float darkLater) {
    float n = r.nightLevel();
    float dark = n * 0.72f + r.rain * 0.18f;
    // if more darkness goes over the whole screen later (the cockpit's), only add what is missing
    if (darkLater > 0) dark = 1 - (1 - clampf(dark, 0, 0.98f)) / (1 - clampf(darkLater, 0, 0.95f));
    if (dark < 0.02f) return;
    gfx::setDepthTest(false); gfx::setBlend(true);
    gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(4, 6, 16, (int)(dark * 255)));
    gfx::setDepthTest(true); gfx::setBlend(false);
}

// one car's headlights on the road ahead: a fan of light fading out to ~45 m (Vita: brighter, with a
// soft pool where the lamps meet the road)
void Renderer::drawBeamFor(const Car& c, const Camera& cam, float k, bool pool) {
    WorldVertex* v = (WorldVertex*)gfx::frameAlloc(18 * sizeof(WorldVertex));
    if (!v) return;
    float half = c.def->lengthM * 0.5f;
    V3 base = c.pos - cam.eye + c.up * 0.08f;
    u32 near_ = RGBA(255, 240, 200, (int)(95 * k)), far_ = RGBA(255, 240, 200, 0);
    V3 a = base + c.fwd * half + c.left * 0.8f, b = base + c.fwd * half - c.left * 0.8f;
    V3 cc = base + c.fwd * (half + 45) - c.left * 7.0f, d = base + c.fwd * (half + 45) + c.left * 7.0f;
    V3 m1 = base + c.fwd * (half + 14) - c.left * 3.0f, m2 = base + c.fwd * (half + 14) + c.left * 3.0f;
    u32 mid = RGBA(255, 240, 200, (int)(70 * k));
    WorldVertex A{0, 0, near_, a.x, a.y, a.z}, B{0, 0, near_, b.x, b.y, b.z}, M1{0, 0, mid, m1.x, m1.y, m1.z}, M2{0, 0, mid, m2.x, m2.y, m2.z};
    WorldVertex C{0, 0, far_, cc.x, cc.y, cc.z}, D{0, 0, far_, d.x, d.y, d.z};
    v[0] = A; v[1] = B; v[2] = M1; v[3] = A; v[4] = M1; v[5] = M2;
    v[6] = M2; v[7] = M1; v[8] = C; v[9] = M2; v[10] = C; v[11] = D;
    gfx::flushCache(v, 12 * sizeof(WorldVertex));
    gfx::bindTexture(nullptr);
    gfx::setBlend(true); gfx::setDepthWrite(false);
    clip::drawWorld(v, 12, M4::identity());
    if (pool) {
        // the bright patch 6-22 m ahead (the soft round puff texture, flat on the road)
        V3 pc = base + c.fwd * (half + 13);
        V3 ax = c.fwd * 10.0f, ay = c.left * 5.5f;
        u32 pcol = RGBA(255, 238, 205, (int)(62 * k));
        V3 p0 = pc - ax - ay, p1 = pc + ax - ay, p2 = pc + ax + ay, p3 = pc - ax + ay;
        WorldVertex P0{0, 1, pcol, p0.x, p0.y, p0.z}, P1{1, 1, pcol, p1.x, p1.y, p1.z}, P2{1, 0, pcol, p2.x, p2.y, p2.z}, P3{0, 0, pcol, p3.x, p3.y, p3.z};
        v[12] = P0; v[13] = P1; v[14] = P2; v[15] = P0; v[16] = P2; v[17] = P3;
        gfx::flushCache(v + 12, 6 * sizeof(WorldVertex));
        gfx::bindTexture(&puff);
        clip::drawWorld(v + 12, 6, M4::identity());
    }
    gfx::setDepthWrite(true); gfx::setBlend(false);
}

void Renderer::drawBeam(const Race& r, const Camera& cam) {
    // headlights on the road (drawn after the darkness, so they light it up). Formula 1 cars have none:
    // night races are floodlit
    float n = r.nightLevel();
    if (n < 2.0f) return;
    bool vita = input::hasTouch();
    drawBeamFor(r.player(), cam, vita ? n * 1.15f : n, vita);
    if (!vita) return;
    for (const Car& c : r.cars) {
        if (c.player) continue;
        V3 rel = c.pos - cam.eye;
        if (dot(rel, rel) > 70.0f * 70.0f || dot(rel + c.fwd * 20.0f, cam.fwd) < 0) continue;
        drawBeamFor(c, cam, n * 0.75f, true);
    }
}

// rain streaks over the view. In the cockpit this is drawn before the cabin, so the rain is only seen
// outside the car (through the glass); the drops on the windscreen are drawWindscreen's.
void Renderer::drawRain(const Race& r, bool cockpit) {
    gfx::setDepthTest(false); gfx::setBlend(true);
    if (r.rain > 0.05f) {
        static gfx::Sprite2D drops[260];
        int n = (int)(r.rain * (perf.effects ? 240 : 90));
        float tt = r.time;
        u32 col = RGBA(190, 200, 220, cockpit ? 85 : 100);
        for (int i = 0; i < n; i++) {
            // each streak falls down the screen at its own speed, wrapping
            uint32_t h = (uint32_t)i * 2654435761u;
            float x = (float)(h % SCREEN_W) + sinf(tt * 0.7f + i) * 3;
            float speed = 520 + (h >> 20) % 300;
            float y = fmodf((float)((h >> 8) % SCREEN_H) + tt * speed, (float)SCREEN_H + 20) - 20;
            drops[i] = gfx::Sprite2D{x, y, 1, 9.0f + (h >> 26) % 7, 0, 0, 0, 0, col};
        }
        gfx::bindTexture(nullptr);
        gfx::drawSprites2D(drops, n);
    }
    gfx::setBlend(false); gfx::setDepthTest(true);
}

void Renderer::drawWeather(const Race& r, bool cockpit) {
    if (!cockpit) drawRain(r, false);          // (in the car it was drawn before the cabin)
    // the lightning flash lights everything, the cabin too
    gfx::setDepthTest(false); gfx::setBlend(true);
    if (r.lightning > 0.02f) gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(235, 240, 255, (int)(r.lightning * 170)));
    gfx::setBlend(false); gfx::setDepthTest(true);
}

// ------------------------------------------------------------------ TV cameras (highlights / team view)
// does any trackside object (tree, grandstand, sign...) stand between a and b?
bool Renderer::sightBlocked(const Track& t, const V3& a, const V3& b) const {
    V3 ab = b - a;
    float L = len(ab);
    if (L < 1) return false;
    for (const PropCell& cell : t.propCells) {
        // skip cells far from the line of sight
        V3 ac = cell.centre - a;
        float k = clampf(dot(ac, ab) / (L * L), 0, 1);
        V3 q = a + ab * k - cell.centre;
        if (q.x * q.x + q.y * q.y > (cell.radius + 15) * (cell.radius + 15)) continue;
        for (int pi = cell.first; pi < cell.first + cell.count; pi++) {
            const PropInst& p = t.props[pi];
            const Model& md = prop[p.model];
            if (!md.lodCount || p.model == PROP_BRIDGE || p.model == PROP_GANTRY) continue;   // the TV camera sees under a footbridge or the gantry
            float rad = fmaxf(md.scale[0], md.scale[1]) * p.scale * 0.8f;
            float top = p.p.z + md.scale[2] * 2 * p.scale;
            // closest approach of the line to the prop's upright axis (in the ground plane)
            V3 ap = p.p - a;
            float kk = clampf((ap.x * ab.x + ap.y * ab.y) / fmaxf(ab.x * ab.x + ab.y * ab.y, 1e-3f), 0, 1);
            float dx = a.x + ab.x * kk - p.p.x, dy = a.y + ab.y * kk - p.p.y;
            if (dx * dx + dy * dy > rad * rad) continue;
            float z = a.z + ab.z * kk;
            if (z < top && z > p.p.z - 1) return true;
        }
    }
    return false;
}

Camera Renderer::tvCamera(const Race& r, int focus, float dt) {
    const Car& c = r.cars[focus];
    const Track& tr = r.track;
    Camera cam;
    cam.up = V3(0, 0, 1);
    float rel = tr.wrap(tvS - c.s);
    if (rel > tr.length * 0.5f) rel -= tr.length;
    V3 target = c.pos + V3(0, 0, 0.8f) + c.fwd * (c.v * 0.12f);
    // the view to the car is checked a few times a second; if trees or stands are in the way, cut
    tvCheck -= dt;
    if (tvInit && tvCheck <= 0) {
        tvCheck = 0.2f;
        tvBlocked = sightBlocked(tr, tvPos, target) ? tvBlocked + 1 : 0;
    }
    // a new trackside camera once the car has gone 70 m past, is far away, or can't be seen
    if (!tvInit || rel < -70 || rel > 400 || focus != tvFocus || tvBlocked >= 3) {
        // try spots along the stretch ahead of the car; take the first with a clear view of the
        // road the car is about to drive (several points along it), else the least blocked one
        float bestScore = 1e9f; V3 best; float bestS = 0;
        for (int k = 0; k < 12; k++) {
            int cut = tvCut + k;
            float s = tr.wrap(c.s + 120 + (cut % 4) * 25);
            int i = tr.index(s);
            float side = (cut & 1) ? 1.0f : -1.0f;
            float run = side > 0 ? tr.pts[i].outL : tr.pts[i].outR;
            // inside the barrier on the run-off (like a TV tower), or just behind it
            float d = tr.pts[i].hw + (k < 6 ? fminf(run * 0.55f, 9.0f) + 1.5f : fminf(run, 14.0f) + 3.0f);
            float h = 4.0f + ((cut / 2) % 3) * 2.5f + (k >= 6 ? 3.0f : 0);
            V3 pos = tr.world(s, side * d, h);
            float score = 0;
            for (int j = 0; j < 4; j++) {
                float ss = tr.wrap(s - 90 + j * 40);
                V3 at = tr.world(ss, r.line[tr.index(ss)], 0.9f);
                if (sightBlocked(tr, pos, at)) score += j == 1 || j == 2 ? 2 : 1;
            }
            if (score < bestScore) { bestScore = score; best = pos; bestS = s; }
            if (score == 0) break;
        }
        tvPos = best; tvS = bestS;
        tvInit = true; tvFocus = focus; tvCut++; tvBlocked = 0; tvCheck = 0.4f;
    }
    cam.eye = tvPos;
    cam.fwd = norm(target - tvPos);
    float d = len(target - tvPos);
    cam.fov = clampf(1500.0f / fmaxf(d, 10.0f), 9, 55);     // zoom in on the car like a TV camera
    camPos = cam.eye;
    return cam;
}

// ------------------------------------------------------------------ HUD: race control, car status
void Renderer::drawRaceInfo(const Race& r) {
    const Car& c = r.player();
    gfx::setDepthTest(false); gfx::setBlend(true);
    char b[64];
    u32 white = RGBA(255, 255, 255), dim = RGBA(190, 200, 215), panel = RGBA(0, 0, 0, 120);
    // flags
    const char* flag = nullptr; u32 fcol = 0, ftxt = RGBA(20, 20, 20);
    if (r.formation) { flag = "FORMATION LAP"; fcol = RGBA(255, 215, 40); }
    else if (r.sc.state == 1) { flag = "SAFETY CAR"; fcol = RGBA(255, 215, 40); }
    else if (r.sc.state == 2) { flag = "SC IN THIS LAP"; fcol = RGBA(255, 215, 40); }
    else if (r.fcyWarn > 0) { snprintf(b, sizeof b, "FCY IN %d", (int)r.fcyWarn + 1); flag = b; fcol = RGBA(255, 215, 40); }
    else if (r.fcy > 0 || r.zoneAt(c.s)) {
        bool over = c.v > r.slowV + 1.4f;
        snprintf(b, sizeof b, "%s 80%s", r.fcy > 0 ? "FCY" : "SLOW ZONE", c.slowLim ? "  LIMITER ON" : "  - SLOW DOWN");
        flag = b; fcol = over && ((int)(r.time * 4) & 1) ? RGBA(255, 70, 40) : RGBA(255, 215, 40);
    } else if (!r.zones.empty() && r.zoneAt(c.s + fmaxf(150.0f, c.v * 5))) { flag = "SLOW ZONE AHEAD"; fcol = RGBA(255, 215, 40); }
    else if (r.session == SES_RACE && r.time - r.greenTime < 4 && r.greenTime > 0 && r.countdown <= 0) { flag = "GREEN FLAG"; fcol = RGBA(40, 200, 70); ftxt = white; }
    else if (c.blue > 0 && ((int)(r.time * 2.5f) & 1)) { flag = "BLUE FLAG"; fcol = RGBA(30, 90, 255); ftxt = white; }
    if (flag && !(r.countdown > 0 && r.set.startType != START_LEMANS)) {
        float w = font.width(flag, 1.0f) + 20;
        gfx::drawFill2D(SCREEN_W / 2 - w / 2, 50, w, 20, fcol);
        font.drawCentre(SCREEN_W / 2, 51, flag, ftxt, 1.0f);
    }
    // race control messages
    for (int i = 0; i < (int)r.msgs.size(); i++) {
        const RaceMsg& m = r.msgs[i];
        float a = clampf(m.time, 0, 1);
        float w = font.width(m.text, 0.8f) + 14;
        float y = 74 + i * 17;
        gfx::drawFill2D(SCREEN_W / 2 - w / 2, y, w, 15, RGBA(0, 0, 0, (int)(150 * a)));
        font.drawCentre(SCREEN_W / 2, y, m.text, (m.color & 0xFFFFFFu) | ((u32)(255 * a) << 24), 0.8f);
    }
    if (c.startDelay > 0 && r.countdown <= 0) font.drawCentre(SCREEN_W / 2, 150, "RUNNING TO THE CAR...", RGBA(255, 220, 60), 1.1f);
    if (r.session != SES_RACE) { gfx::setBlend(false); gfx::setDepthTest(true); return; }
    // car status panel (left): tyres, fuel, damage
    float x = 6, y = 52;
    gfx::drawFill2D(x, y, 80, 92, panel);
    static const u32 TYC[TY_COUNT] = {RGBA(235, 40, 40), RGBA(250, 210, 30), RGBA(240, 240, 240), RGBA(40, 200, 60), RGBA(40, 120, 255)};
    gfx::drawFill2D(x + 5, y + 5, 8, 8, TYC[c.tyre]);
    font.draw(x + 17, y + 2, TYRE_NAME[c.tyre], white, 0.7f);
    gfx::drawFill2D(x + 5, y + 17, 70, 4, RGBA(50, 50, 55));
    float life = 1 - c.wear;
    gfx::drawFill2D(x + 5, y + 17, 70 * life, 4, life > 0.4f ? RGBA(80, 220, 90) : (life > 0.15f ? RGBA(250, 200, 40) : RGBA(250, 60, 40)));
    font.draw(x + 5, y + 23, "FUEL", dim, 0.6f);
    gfx::drawFill2D(x + 30, y + 27, 45, 4, RGBA(50, 50, 55));
    gfx::drawFill2D(x + 30, y + 27, 45 * c.fuel, 4, c.fuel > 0.15f ? RGBA(80, 180, 255) : RGBA(250, 60, 40));
    drawDamage(c, x + 40, y + 38, r.time);
    // tyre temperatures beside the car diagram: blue cold, green in the window, red too hot
    for (int k = 0; k < 4; k++) {
        float T = c.tyreT[k];
        u32 tc = T < 65 ? RGBA(70, 130, 255) : (T < 80 ? RGBA(90, 200, 230) : (T <= 105 ? RGBA(80, 220, 90) : (T < 115 ? RGBA(250, 190, 40) : RGBA(250, 60, 40))));
        float tx = x + ((k & 1) ? 56 : 12), ty = y + (k < 2 ? 42 : 68);
        gfx::drawFill2D(tx, ty, 12, 14, tc);
        if (input::hasTouch()) {     // the Vita's sharper screen: the number too
            snprintf(b, sizeof b, "%d", (int)(T + 0.5f));
            font.drawCentre(tx + 6, ty + 2, b, RGBA(10, 10, 10), 0.42f);
        }
    }
    if (input::hasTouch()) drawRelative(r);
    // penalties / pit requests
    float py = y + 96;
    if (c.penalty > 0) { snprintf(b, sizeof b, "+%.0fs PEN", c.penalty); font.draw(x, py, b, RGBA(255, 120, 60), 0.75f); py += 13; }
    if (c.driveThrough) { snprintf(b, sizeof b, "DRIVE-THRU %d", c.dtLaps); font.draw(x, py, b, RGBA(255, 90, 60), 0.75f); py += 13; }
    if (c.su.map != MAP_NORMAL) { snprintf(b, sizeof b, "MAP %s", c.su.map == MAP_ATTACK ? "ATTACK" : "SC"); font.draw(x, py, b, c.su.map == MAP_ATTACK ? RGBA(255, 120, 60) : RGBA(255, 215, 40), 0.7f); py += 13; }
    if (c.limitWarn > 0) { snprintf(b, sizeof b, "LIMITS %d/5", c.limitWarn); font.draw(x, py, b, RGBA(255, 255, 255), 0.7f); py += 13; }
    { float f = c.drv[c.driver].fatigue;
      if (f > 0.5f) { snprintf(b, sizeof b, "TIRED %d%%", (int)(f * 100)); font.draw(x, py, b, f > 0.7f ? RGBA(255, 90, 60) : RGBA(255, 200, 60), 0.7f); py += 13; } }
    if (c.wantPit) { font.draw(x, py, ((int)(r.time * 3) & 1) ? "BOX THIS LAP" : "", RGBA(255, 220, 60), 0.75f); py += 13; }
    if (c.pit == PIT_IN || c.pit == PIT_OUT) font.drawCentre(SCREEN_W / 2, SCREEN_H - 50, "PIT LIMITER 60", RGBA(255, 220, 60), 0.9f);
    if (r.timeAccel > 1) { snprintf(b, sizeof b, "x%d", (int)r.timeAccel); font.drawRight(SCREEN_W - 12, 56, b, RGBA(120, 200, 255), 1.0f); }
    gfx::setBlend(false); gfx::setDepthTest(true);
}

// ------------------------------------------------------------------ manual pit stop prompts
void Renderer::drawQte(const Race& r) {
    const PitQte& q = r.qte;
    const Car& c = r.player();
    if (!q.on || c.pit != PIT_STOP) return;
    gfx::setDepthTest(false); gfx::setBlend(true);
    static const char* LABEL[8] = {"REFUEL", "AIR JACKS", "FRONT LEFT", "FRONT RIGHT", "REAR LEFT", "REAR RIGHT", "REPAIRS", "RELEASE"};
    int st = q.idx < q.n ? q.seq[q.idx] : QS_DROP;
    float x0 = SCREEN_W / 2 - 130, y0 = 156;
    gfx::drawFill2D(x0, y0, 256, 80, RGBA(0, 0, 0, 170));
    char b[48];
    snprintf(b, sizeof b, "STOP  %.1f s", c.pitTotal);
    font.drawRight(x0 + 250, y0 + 3, b, RGBA(255, 220, 60), 0.8f);
    font.draw(x0 + 8, y0 + 3, LABEL[st], RGBA(255, 255, 255), 0.9f);
    // jobs: done / now / to come
    for (int i = 0; i < q.n; i++) {
        u32 col = i < q.idx ? RGBA(80, 220, 100) : (i == q.idx ? RGBA(255, 220, 60) : RGBA(70, 70, 80));
        gfx::drawFill2D(x0 + 8 + i * 14, y0 + 22, 11, 4, col);
    }
    float bx = x0 + 56, by = y0 + 40, bw = 190, bh = 14;
    if (q.waiting) {
        // the button to press
        static const u32 BC[4] = {RGBA(120, 165, 255), RGBA(255, 80, 80), RGBA(255, 130, 200), RGBA(80, 220, 160)};
        static const char* BG[4] = {"X", "O", "[]", "/\\"};
        gfx::drawFill2D(x0 + 10, y0 + 32, 34, 30, RGBA(30, 30, 38, 240));
        font.drawCentre(x0 + 27, y0 + 33, BG[q.button], BC[q.button], 1.4f);
        // bar: green zone, perfect band, marker
        gfx::drawFill2D(bx, by, bw, bh, RGBA(60, 60, 70, 230));
        gfx::drawFill2D(bx + bw * q.z0, by, bw * (q.z1 - q.z0), bh, RGBA(40, 170, 70, 255));
        gfx::drawFill2D(bx + bw * q.p0, by, bw * (q.p1 - q.p0), bh, RGBA(120, 255, 140, 255));
        gfx::drawFill2D(bx + bw * clampf(q.pos, 0, 1) - 1.5f, by - 4, 3, bh + 8, RGBA(255, 255, 255));
    } else {
        // the job under way
        float k = q.workTotal > 0 ? clampf(1 - q.work / q.workTotal, 0, 1) : 1;
        gfx::drawFill2D(bx, by, bw, bh, RGBA(60, 60, 70, 230));
        gfx::drawFill2D(bx, by, bw * k, bh, RGBA(80, 180, 255));
        font.draw(x0 + 10, y0 + 38, st == QS_REPAIR ? "CREW" : "WORK", RGBA(190, 200, 215), 0.7f);
    }
    if (q.flash > 0 && q.lastShown >= 0) {
        static const char* R[3] = {"FUMBLE!", "GOOD", "PERFECT!"};
        static const u32 RC[3] = {RGBA(255, 90, 60), RGBA(255, 220, 60), RGBA(80, 255, 120)};
        font.drawCentre(SCREEN_W / 2, y0 - 22, R[q.lastShown], RC[q.lastShown], 1.3f);
    }
    font.drawCentre(SCREEN_W / 2, y0 + 64, q.waiting ? "Press the button once, in the green" : "", RGBA(190, 200, 215), 0.66f);
    if (input::hasTouch()) {
        // PS Vita: four touch pads along the bottom answer the prompt like the face buttons
        static const u32 BC[4] = {RGBA(120, 165, 255), RGBA(255, 80, 80), RGBA(255, 130, 200), RGBA(80, 220, 160)};
        static const char* BG[4] = {"X", "O", "[]", "/\\"};
        for (int b = 0; b < 4; b++) {
            float x = SCREEN_W / 2 - 130 + b * 65;
            bool lit = q.waiting && q.button == b;
            gfx::drawFill2D(x, 240, 60, 30, lit ? RGBA(60, 60, 75, 235) : RGBA(25, 25, 32, 200));
            gfx::drawFill2D(x, 240, 60, 2, BC[b]);
            font.drawCentre(x + 30, 245, BG[b], BC[b], 1.2f);
        }
    }
    gfx::setBlend(false); gfx::setDepthTest(true);
}

// damage diagram (top view): nose, sides, wing, suspension, engine
// PS Vita HUD: the cars just ahead and behind (gap in seconds, class colour), stint time and
// how many laps the fuel lasts. Right side, under the timing panel.
void Renderer::drawRelative(const Race& r) {
    const Car& c = r.player();
    const Car* ahead = nullptr; const Car* behind = nullptr;
    for (const Car& o : r.cars) {
        if (o.position == c.position - 1) ahead = &o;
        if (o.position == c.position + 1) behind = &o;
    }
    float x = SCREEN_W - 128, y = 72, w = 122;
    gfx::drawFill2D(x, y, w, 58, RGBA(0, 0, 0, 120));
    char b[48];
    auto line = [&](const Car* o, float ly, bool front) {
        if (!o) { font.draw(x + 5, ly, front ? "LEADING" : "LAST", RGBA(190, 200, 215), 0.62f); return; }
        float dist = front ? o->total - c.total : c.total - o->total;
        float lapLen = r.track.length;
        u32 cc = o->def->cls == CLASS_HYPER ? RGBA(255, 90, 80) : RGBA(255, 170, 50);
        gfx::drawFill2D(x + 5, ly + 3, 4, 8, cc);
        if (dist > lapLen) snprintf(b, sizeof b, "%s%d LAP", front ? "+" : "-", (int)(dist / lapLen));
        else snprintf(b, sizeof b, "%s%.1f", front ? "+" : "-", dist / fmaxf(c.v, 25.0f));
        font.draw(x + 12, ly, b, front ? RGBA(255, 255, 255) : RGBA(190, 200, 215), 0.66f);
        snprintf(b, sizeof b, "#%d %s", o->number, o->def->make);
        font.drawRight(x + w - 5, ly, b, RGBA(190, 200, 215), 0.62f);
    };
    line(ahead, y + 2, true);
    line(behind, y + 15, false);
    // stint and fuel
    float st = c.stint;
    float lf = r.lapFuel(c);
    float laps = lf > 0 ? c.fuel / lf : 0;
    font.draw(x + 5, y + 30, "STINT", RGBA(140, 190, 255), 0.6f);
    snprintf(b, sizeof b, "%d:%02d", (int)st / 3600, ((int)st / 60) % 60);
    font.drawRight(x + w - 5, y + 30, b, RGBA(255, 255, 255), 0.62f);
    font.draw(x + 5, y + 42, "FUEL", RGBA(140, 190, 255), 0.6f);
    snprintf(b, sizeof b, "%.1f laps", laps);
    font.drawRight(x + w - 5, y + 42, b, laps < 1.5f ? RGBA(255, 90, 60) : RGBA(255, 255, 255), 0.62f);
}

void Renderer::drawDamage(const Car& c, float cx, float cy, float time) {
    auto dc = [&](float d, bool gone) -> u32 {
        if (gone) return ((int)(time * 4) & 1) ? RGBA(90, 90, 90) : RGBA(30, 30, 30);
        if (d < 0.05f) return RGBA(60, 190, 80, 200);
        if (d < 0.4f) return RGBA(240, 210, 40, 230);
        return RGBA(240, 60, 40, 240);
    };
    gfx::drawFill2D(cx - 7, cy, 14, 8, dc(c.dmg[DM_NOSE], c.noseOff));
    gfx::drawFill2D(cx - 11, cy + 9, 4, 26, dc(c.dmg[DM_LEFT], false));
    gfx::drawFill2D(cx + 7, cy + 9, 4, 26, dc(c.dmg[DM_RIGHT], false));
    gfx::drawFill2D(cx - 6, cy + 13, 12, 12, dc(c.dmg[DM_ENGINE], false));
    gfx::drawFill2D(cx - 12, cy + 37, 24, 5, dc(c.dmg[DM_WING], c.wingOff));
    gfx::drawFill2D(cx - 6, cy + 27, 12, 6, dc(c.dmg[DM_SUSP], false));
}

// the cockpit camera has no HUD: only the damage diagram and the pit-in indicator are overlaid
// (plus the prompts of a manual pit stop)
// marshals wave blue flags at the posts just ahead while a faster car is closing on the player
void Renderer::drawMarshalFlags(const Race& r, const Camera& cam) {
    const Car& me = r.player();
    bool yellow = r.fcy > 0 || !r.zones.empty();
    if (me.blue <= 0 && !yellow) return;
    gfx::setFog(false, 0, 1, 0);
    for (const PropInst& pr : r.track.props) {
        if (pr.model != PROP_MARSHAL) continue;
        V3 rel = pr.p - me.pos;
        float ahead = dot(rel, me.fwd);
        if (ahead < 15 || ahead > 450 || dot(rel, rel) > 470.0f * 470.0f) continue;
        // yellow at the posts in a slow zone / under the FCY, else blue (only when shown to us)
        bool slow = yellow && r.slowAt(me.s + ahead);
        if (!slow && me.blue <= 0) continue;
        u32 flagCol = slow ? RGBA(255, 215, 30) : RGBA(30, 90, 255);
        V3 base = pr.p - cam.eye;
        // the marshal stands at the front of the post, flag on a short pole, waved side to side
        V3 toTrack(-cosf(pr.yaw + 3.14159f), -sinf(pr.yaw + 3.14159f), 0);
        V3 at = base + toTrack * 1.5f + V3(0, 0, 2.2f);   // drawn a little larger than life so it reads on a small screen
        float wave = sinf(r.time * 7.0f + pr.p.x * 0.1f) * 0.6f;
        V3 along = norm(V3(-toTrack.y, toTrack.x, 0) * cosf(wave) + V3(0, 0, 1) * sinf(wave) * 0.5f);
        V3 up = V3(0, 0, 1);
        M4 pole = M4::basis(along, up, cross(along, up), at);
        quad3D(pole, -0.06f, -2.2f, 0.06f, 2.6f, RGBA(220, 220, 220));
        quad3D(pole * M4::translate(V3(0, 1.0f, 0.01f)), 0.06f, 0, 3.0f, 1.6f, flagCol);
        quad3D(pole * M4::translate(V3(0, 1.0f, -0.01f)), 0.06f, 0, 3.0f, 1.6f, flagCol);
    }
}

void Renderer::drawCockpitHud(const Race& r) {
    const Car& c = r.player();
    gfx::setDepthTest(false); gfx::setFog(false, 0, 1, 0); gfx::setBlend(true);
    // (Formula Legends: the timing HUD stays on in the cockpit, with the damage diagram)
    const char* pit = nullptr;
    if (c.pit == PIT_IN || c.pit == PIT_OUT) pit = "PIT LIMITER";
    else if (c.wantPit && ((int)(r.time * 3) & 1)) pit = "PIT IN";
    if (pit) {
        float w = font.width(pit, 0.9f) + 16;
        gfx::drawFill2D(SCREEN_W - w - 6, 6, w, 20, RGBA(0, 0, 0, 150));
        font.draw(SCREEN_W - w + 2, 8, pit, RGBA(255, 220, 60), 0.9f);
    }
    drawQte(r);
    gfx::setBlend(false); gfx::setDepthTest(true);
}

// pit stops and the garage: a slow TV shot from the pit lane, beside the car
Camera Renderer::cinematicCamera(const Race& r, const Car& c, float t) {
    Camera cam;
    V3 p, tg, l; float hw;
    r.track.frame(c.s, p, tg, l, hw);
    V3 out = l * -1.0f;                     // from the car towards the fast lane / track
    float sw = sinf(t * 0.22f);
    float dist = 5.2f;
    cam.eye = c.pos + out * dist + tg * (sw * 3.4f + 1.0f) + V3(0, 0, 1.7f + 0.3f * cosf(t * 0.17f));
    if (c.pitPhase <= -2) {
        // looking straight into the garage from the pit lane (the car sits 3 m off the garage's middle)
        float offMid = (c.number % 2) * 6.0f - 3.0f;
        cam.eye = c.pos + out * (fabsf(c.d - r.boxD) + 3.2f) + tg * (-offMid + sw * 1.2f) + V3(0, 0, 1.8f);
    }
    V3 target = c.pos + V3(0, 0, 0.6f);
    cam.fwd = norm(target - cam.eye); cam.up = V3(0, 0, 1);
    cam.fov = 50;
    camPos = cam.eye;
    return cam;
}
