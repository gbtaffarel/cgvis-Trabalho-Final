#ifndef _BALL_MOVEMENT_H
#define _BALL_MOVEMENT_H

#include <cmath>
#include <cstdio>
#include <set>
#include <algorithm>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <GLFW/glfw3.h>

#include "matrices.h"

// Constantes geométricas e físicas do mundo (estilo Kula World)
const float BLOCK_SIZE       = 2.0f;  // Lado de cada bloco cúbico
const float BLOCK_HALF       = 1.0f;  // Metade do lado do bloco (centro até face)
const float BALL_RADIUS      = 0.42f; // Raio da bola

// Parâmetros cinemáticos baseados no tempo (Delta t)
const float BALL_MAX_SPEED   = 4.2f;  // Velocidade máxima de rolamento (unidades/s)
const float BALL_ACCEL       = 9.5f;  // Aceleração ao pressionar W (unidades/s^2)
const float BALL_BRAKE_ACCEL = 18.0f; // Desaceleração de frenagem ao pressionar S
const float BALL_FRICTION    = 8.0f;  // Desaceleração natural por atrito ao soltar teclas
const float BALL_WRAP_SPEED  = 3.2f;  // Velocidade mínima durante transição de borda convexa

// Parâmetros do salto com curva de Bézier cúbica e deformação (squish)
const float JUMP_DURATION    = 0.58f; // Duração do salto (segundos)
const float JUMP_MAX_HEIGHT  = 1.45f; // Altura máxima atingida no salto
const float LAND_SQUISH_TIME = 0.24f; // Duração da animação de squish no pouso
const float FALL_GRAVITY     = 18.0f; // Aceleração de queda livre fora da plataforma

// Coordenada discreta de um bloco no grid 3D
struct GridCoord
{
    int x, y, z;

    bool operator<(const GridCoord& o) const
    {
        if (x != o.x) return x < o.x;
        if (y != o.y) return y < o.y;
        return z < o.z;
    }

    bool operator==(const GridCoord& o) const
    {
        return x == o.x && y == o.y && z == o.z;
    }
};

// Modos de câmera disponíveis
enum CameraMode
{
    CAMERA_THIRD_PERSON = 0, // Câmera orbital em 3ª pessoa seguindo a bola (4 direções + gravidade dinâmica)
    CAMERA_FIRST_PERSON = 1, // Câmera em 1ª pessoa na frente da bola
    CAMERA_FREE_ORBIT   = 2  // Câmera orbital livre controlada pelo mouse
};

// Estrutura que armazena todo o estado cinemático e de orientação da bola
struct BallState
{
    // Bloco atual ao qual a bola está aderida
    GridCoord current_block;

    // Referencial ortonormal local sobre a superfície atual:
    //   up      : normal da superfície (gravidade aponta para -up)
    //   forward : direção frontal atual de movimento (gira em passos de 90 graus)
    glm::vec4 up;
    glm::vec4 forward;

    // Deslocamento tangencial do ponto de contato em relação ao centro da face atual
    glm::vec4 surface_offset;

    // Posição efetiva do centro da bola em coordenadas do mundo (World)
    glm::vec4 position_world;

    // Velocidade escalar ao longo do vetor forward
    float speed;

    // Matriz de rotação acumulada pelo rolamento físico (Delta theta = Delta S / R)
    glm::mat4 rotation_matrix;

    // Estado de transição suave em borda convexa de 90 graus
    bool      is_wrapping_edge;
    float     wrap_angle;       // Ângulo percorrido na borda [0, pi/2]
    glm::vec4 wrap_pivot;       // Ponto na quina do bloco em torno do qual a bola gira
    glm::vec4 wrap_start_up;    // Normal inicial antes da dobra
    glm::vec4 wrap_move_dir;    // Sentido do movimento que iniciou a dobra
    int       wrap_move_sign;   // +1 se movendo para frente, -1 se para trás

    // Estado do salto com curva de Bézier cúbica
    bool  is_jumping;
    float jump_timer;           // Tempo decorrido no salto atual
    float jump_elevation;       // Elevação atual calculada pela Bézier cúbica

    // Estado de queda livre (quando sai das plataformas sem bloco abaixo)
    bool      is_falling;
    float     fall_speed;
    float     fall_distance;

    // Estado da deformação elástica (squish)
    float squish_normal_scale;  // Escala na direção da normal da superfície
    float squish_tangent_scale; // Escala no plano tangente (preservação de volume)
    float landing_timer;        // Timer pós-pouso para oscilação de squash & stretch

    // Vetores suavizados para a câmera acompanhar giros de 90° e trocas de gravidade
    glm::vec4 cam_up;
    glm::vec4 cam_forward;
    int       cam_orbit_quadrant; // 0, 1, 2, 3 (4 direções orbitais fixas ao redor de up)
};

// Conjunto global de blocos que compõem o nível de teste
inline std::set<GridCoord>& GetLevelBlocks()
{
    static std::set<GridCoord> blocks;
    return blocks;
}

inline bool HasBlock(int x, int y, int z)
{
    GridCoord c = {x, y, z};
    return GetLevelBlocks().count(c) > 0;
}

inline GridCoord OffsetGrid(GridCoord c, glm::vec4 dir)
{
    GridCoord res = {
        c.x + (int)std::round(dir.x),
        c.y + (int)std::round(dir.y),
        c.z + (int)std::round(dir.z)
    };
    return res;
}

inline bool HasBlockAt(GridCoord c)
{
    return GetLevelBlocks().count(c) > 0;
}

// Centro do bloco em coordenadas do mundo
inline glm::vec4 BlockCenterWorld(GridCoord c)
{
    return glm::vec4(
        c.x * BLOCK_SIZE,
        c.y * BLOCK_SIZE,
        c.z * BLOCK_SIZE,
        1.0f
    );
}

// Inicializa o cenário de blocos 3D para testar:
// 1) Rolamento reto e curvas de 90° (anel 3x3 com furo central, como na image3 da SPEC)
// 2) Subida em parede vertical (canto côncavo) e arco suspenso (image2 e image3 da SPEC)
// 3) Dobras em bordas externas (canto convexo) permitindo andar nas laterais e embaixo dos blocos
// 4) Salto sobre vão (gap) com curva de Bézier cúbica
inline void InitLevelBlocks()
{
    std::set<GridCoord>& b = GetLevelBlocks();
    b.clear();

    // Passarela inicial (Z = 0 até Z = -3)
    b.insert({0, 0,  0});
    b.insert({0, 0, -1});
    b.insert({0, 0, -2});
    b.insert({0, 0, -3});

    // Plataforma em anel 3x3 com furo no meio (Z = -3 a -5, X = 0 a 2)
    b.insert({1, 0, -3});
    b.insert({2, 0, -3});
    b.insert({0, 0, -4});
    b.insert({2, 0, -4});
    b.insert({0, 0, -5});
    b.insert({1, 0, -5});
    b.insert({2, 0, -5});

    // Arco vertical subindo em Z = -5 (permite subir pela parede e andar no teto/topo do arco)
    b.insert({0, 1, -5});
    b.insert({0, 2, -5});
    b.insert({1, 2, -5});
    b.insert({2, 2, -5});
    b.insert({2, 1, -5});

    // Rampa de impulso para salto em X = 1, indo para -Z
    b.insert({1, 0, -6});
    // Vão (gap) em {1, 0, -7} para testar o salto com curva de Bézier cúbica!
    // Ilha de chegada após o salto:
    b.insert({1, 0, -8});
    b.insert({1, 0, -9});
    b.insert({0, 0, -9});
    b.insert({2, 0, -9});
    b.insert({1, 1, -9}); // Parede no fundo da ilha
}

// Avalia um ponto sobre uma curva de Bézier cúbica para t em [0, 1]:
//   B(t) = (1-t)^3 * P0 + 3*(1-t)^2*t * P1 + 3*(1-t)*t^2 * P2 + t^3 * P3
inline glm::vec4 EvaluateCubicBezier(
    const glm::vec4& p0,
    const glm::vec4& p1,
    const glm::vec4& p2,
    const glm::vec4& p3,
    float t
)
{
    float u  = 1.0f - t;
    float b0 = u * u * u;
    float b1 = 3.0f * u * u * t;
    float b2 = 3.0f * u * t * t;
    float b3 = t * t * t;
    return b0 * p0 + b1 * p1 + b2 * p2 + b3 * p3;
}

// Arredonda um vetor unitário próximo aos eixos cartesianos para evitar erro numérico acumulado
inline glm::vec4 SnapToAxis(glm::vec4 v)
{
    glm::vec4 res(0.0f, 0.0f, 0.0f, 0.0f);
    float ax = std::fabs(v.x);
    float ay = std::fabs(v.y);
    float az = std::fabs(v.z);
    if (ax >= ay && ax >= az)
        res.x = (v.x >= 0.0f) ? 1.0f : -1.0f;
    else if (ay >= ax && ay >= az)
        res.y = (v.y >= 0.0f) ? 1.0f : -1.0f;
    else
        res.z = (v.z >= 0.0f) ? 1.0f : -1.0f;
    return res;
}

// Reinicia a bola na posição inicial do nível
inline void ResetBallState(BallState& ball)
{
    ball.current_block        = {0, 0, 0};
    ball.up                   = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
    ball.forward              = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
    ball.surface_offset       = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    ball.speed                = 0.0f;
    ball.rotation_matrix      = Matrix_Identity();

    ball.is_wrapping_edge     = false;
    ball.wrap_angle           = 0.0f;
    ball.wrap_pivot           = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    ball.wrap_start_up        = ball.up;
    ball.wrap_move_dir        = ball.forward;
    ball.wrap_move_sign       = 1;

    ball.is_jumping           = false;
    ball.jump_timer           = 0.0f;
    ball.jump_elevation       = 0.0f;

    ball.is_falling           = false;
    ball.fall_speed           = 0.0f;
    ball.fall_distance        = 0.0f;

    ball.squish_normal_scale  = 1.0f;
    ball.squish_tangent_scale = 1.0f;
    ball.landing_timer        = 0.0f;

    ball.cam_up               = ball.up;
    ball.cam_forward          = ball.forward;
    ball.cam_orbit_quadrant   = 0;

    ball.position_world       = BlockCenterWorld(ball.current_block)
                              + (BLOCK_HALF + BALL_RADIUS) * ball.up;
}

// Acumula rotação física de rolamento sem deslizamento: Delta theta = Delta S / R
inline void AccumulateRollingRotation(BallState& ball, glm::vec4 surface_normal, glm::vec4 move_dir, float distance)
{
    if (std::fabs(distance) < 1e-6f)
        return;

    glm::vec4 axis = crossproduct(surface_normal, move_dir);
    float axis_norm = norm(axis);
    if (axis_norm < 1e-6f)
        return;

    float d_theta = distance / BALL_RADIUS;
    ball.rotation_matrix = Matrix_Rotate(d_theta, axis) * ball.rotation_matrix;
}

// Gira a direção da bola em +90 graus (esquerda) ou -90 graus (direita) em torno da normal atual
inline void TurnBall90Degrees(BallState& ball, bool turn_left)
{
    if (ball.is_wrapping_edge || ball.is_falling)
        return;

    // Vetor direita atual na superfície: right = forward x up
    glm::vec4 right = crossproduct(ball.forward, ball.up);
    if (turn_left)
        ball.forward = SnapToAxis(-right);
    else
        ball.forward = SnapToAxis(right);
}

// Inicia um salto (quando no chão)
inline void StartBallJump(BallState& ball, bool forward_key_held)
{
    if (ball.is_jumping || ball.is_wrapping_edge || ball.is_falling)
        return;

    ball.is_jumping     = true;
    ball.jump_timer     = 0.0f;
    ball.jump_elevation = 0.0f;
    ball.landing_timer  = 0.0f;

    // Se o jogador estiver segurando W ao saltar, garante velocidade horizontal suficiente para cruzar um bloco
    if (forward_key_held && ball.speed < 0.88f * BALL_MAX_SPEED)
        ball.speed = 0.88f * BALL_MAX_SPEED;
}

// Atualiza o fator de deformação (squish) durante o salto e no impacto do pouso
inline void UpdateBallSquish(BallState& ball, float dt)
{
    float sn = 1.0f;

    if (ball.is_jumping)
    {
        float t = ball.jump_timer / JUMP_DURATION;
        if (t < 0.10f)
        {
            // Compressão rápida de impulsão no início do salto
            float u = t / 0.10f;
            sn = 1.0f - 0.28f * std::sin(u * 3.14159265f);
        }
        else if (t < 0.35f)
        {
            // Esticamento (stretch) na subida
            float u = (t - 0.10f) / 0.25f;
            sn = 1.0f + 0.22f * std::sin(u * 3.14159265f);
        }
        else if (t < 0.70f)
        {
            // Próximo ao ápice da curva de Bézier, retorna ao formato esférico
            sn = 1.0f;
        }
        else
        {
            // Leve alongamento na descida preparando o impacto
            float u = (t - 0.70f) / 0.30f;
            sn = 1.0f + 0.16f * std::sin(u * 3.14159265f);
        }
    }
    else if (ball.landing_timer > 0.0f)
    {
        ball.landing_timer = std::max(0.0f, ball.landing_timer - dt);
        float tau = 1.0f - (ball.landing_timer / LAND_SQUISH_TIME); // 0 -> 1
        // Oscilação amortecida: achata no impacto (sn ~ 0.68) e recupera elasticamente
        sn = 1.0f - 0.32f * (1.0f - tau) * std::cos(2.5f * 3.14159265f * tau);
    }

    ball.squish_normal_scale  = sn;
    // Preservação aproximada do volume da esfera: s_t^2 * s_n = 1 => s_t = 1 / sqrt(s_n)
    ball.squish_tangent_scale = 1.0f / std::sqrt(std::max(0.25f, sn));
}

// Constrói a matriz de transformação de deformação (squish) alinhada à normal da superfície
inline glm::mat4 GetBallSquishMatrix(const BallState& ball)
{
    glm::vec4 u = ball.is_wrapping_edge
        ? (std::cos(ball.wrap_angle) * ball.wrap_start_up + std::sin(ball.wrap_angle) * ball.wrap_move_dir)
        : ball.up;
    u = u / norm(u);

    glm::vec4 f = ball.forward - dotproduct(ball.forward, u) * u;
    if (norm(f) < 1e-4f)
        f = crossproduct(u, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    if (norm(f) < 1e-4f)
        f = crossproduct(u, glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    f = f / norm(f);

    glm::vec4 r = crossproduct(f, u);
    r = r / norm(r);

    // Matriz de mudança de base B = [r, u, f]
    glm::mat4 B = Matrix(
        r.x , u.x , f.x , 0.0f ,
        r.y , u.y , f.y , 0.0f ,
        r.z , u.z , f.z , 0.0f ,
        0.0f, 0.0f, 0.0f, 1.0f
    );
    glm::mat4 B_T = Matrix(
        r.x , r.y , r.z , 0.0f ,
        u.x , u.y , u.z , 0.0f ,
        f.x , f.y , f.z , 0.0f ,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    float st = ball.squish_tangent_scale;
    float sn = ball.squish_normal_scale;
    return B * Matrix_Scale(st, sn, st) * B_T;
}

// Constrói a matriz Model completa da bola (Translação * Squish * Escala * Rotação de Rolamento)
inline glm::mat4 GetBallModelMatrix(const BallState& ball)
{
    glm::vec4 u = ball.is_wrapping_edge
        ? (std::cos(ball.wrap_angle) * ball.wrap_start_up + std::sin(ball.wrap_angle) * ball.wrap_move_dir)
        : ball.up;
    u = u / norm(u);

    // Ajuste vertical do centro durante o squish no chão para manter o polo inferior colado na superfície
    float ground_weight = std::max(0.0f, 1.0f - (ball.jump_elevation / BALL_RADIUS));
    float squish_shift  = (ball.squish_normal_scale - 1.0f) * BALL_RADIUS * ground_weight;
    glm::vec4 render_pos = ball.position_world + squish_shift * u;

    return Matrix_Translate(render_pos.x, render_pos.y, render_pos.z)
         * GetBallSquishMatrix(ball)
         * Matrix_Scale(BALL_RADIUS, BALL_RADIUS, BALL_RADIUS)
         * ball.rotation_matrix;
}

// Atualiza toda a física e lógica de movimentação da bola baseada no passo de tempo dt
inline void UpdateBallPhysics(
    BallState& ball,
    float dt,
    bool key_w_pressed,
    bool key_s_pressed
)
{
    // Limitamos dt para evitar saltos grandes caso a janela seja arrastada
    if (dt > 0.05f)
        dt = 0.05f;
    if (dt <= 0.0f)
        return;

    // 1. Caso de queda livre fora das plataformas
    if (ball.is_falling)
    {
        ball.fall_speed    += FALL_GRAVITY * dt;
        float dz            = ball.fall_speed * dt;
        ball.fall_distance += dz;
        ball.position_world = ball.position_world - dz * ball.up;
        AccumulateRollingRotation(ball, ball.up, ball.forward, ball.speed * dt);

        // Verifica se durante a queda encontrou um bloco abaixo
        int steps_down = (int)std::floor((ball.fall_distance + BLOCK_HALF) / BLOCK_SIZE);
        if (steps_down >= 1)
        {
            GridCoord candidate = OffsetGrid(ball.current_block, -((float)steps_down) * ball.up);
            if (HasBlockAt(candidate))
            {
                ball.current_block  = candidate;
                ball.is_falling     = false;
                ball.fall_speed     = 0.0f;
                ball.fall_distance  = 0.0f;
                ball.landing_timer  = LAND_SQUISH_TIME;
                ball.position_world = BlockCenterWorld(ball.current_block)
                                    + (BLOCK_HALF + BALL_RADIUS) * ball.up
                                    + ball.surface_offset;
            }
        }

        if (ball.fall_distance > 12.0f)
        {
            ResetBallState(ball);
        }
        return;
    }

    // 2. Caso de transição suave em borda convexa (dobrando 90 graus para outra face do mesmo bloco)
    if (ball.is_wrapping_edge)
    {
        float wrap_vel = std::max( std::fabs(ball.speed), BALL_WRAP_SPEED );
        float ds       = wrap_vel * dt;
        float d_alpha  = ds / BALL_RADIUS;

        ball.wrap_angle += d_alpha;
        AccumulateRollingRotation(ball, ball.wrap_start_up, ball.wrap_move_dir, ds);

        const float half_pi = 3.14159265f / 2.0f;
        if (ball.wrap_angle >= half_pi)
        {
            float overshoot_dist = (ball.wrap_angle - half_pi) * BALL_RADIUS;
            ball.is_wrapping_edge = false;
            ball.wrap_angle       = 0.0f;

            // Ao completar 90 graus na borda convexa:
            // A nova normal da superfície passa a ser o sentido do movimento antigo (wrap_move_dir)
            // O novo sentido de avanço aponta para -wrap_start_up
            glm::vec4 new_up       = SnapToAxis(ball.wrap_move_dir);
            glm::vec4 new_move_dir = SnapToAxis(-ball.wrap_start_up);

            ball.up      = new_up;
            ball.forward = (ball.wrap_move_sign >= 0) ? new_move_dir : -new_move_dir;

            // Na nova face, a borda recém-cruzada fica em -BLOCK_HALF na direção new_move_dir
            glm::vec4 right = crossproduct(ball.forward, ball.up);
            float lat_offset = dotproduct(ball.surface_offset, right);
            ball.surface_offset = (-BLOCK_HALF + overshoot_dist) * new_move_dir + lat_offset * right;
        }
        else
        {
            glm::vec4 u_inst = std::cos(ball.wrap_angle) * ball.wrap_start_up
                             + std::sin(ball.wrap_angle) * ball.wrap_move_dir;
            ball.position_world = ball.wrap_pivot + BALL_RADIUS * u_inst;
        }
    }
    else
    {
        // 3. Atualização da velocidade linear com aceleração/atrito baseados em dt
        if (key_w_pressed)
        {
            ball.speed += BALL_ACCEL * dt;
            if (ball.speed > BALL_MAX_SPEED)
                ball.speed = BALL_MAX_SPEED;
        }
        else if (key_s_pressed)
        {
            if (ball.speed > 0.0f)
                ball.speed = std::max(0.0f, ball.speed - BALL_BRAKE_ACCEL * dt);
            else
                ball.speed = std::max(-0.55f * BALL_MAX_SPEED, ball.speed - 0.6f * BALL_ACCEL * dt);
        }
        else if (!ball.is_jumping)
        {
            // Desaceleração natural por atrito quando no chão
            if (ball.speed > 0.0f)
                ball.speed = std::max(0.0f, ball.speed - BALL_FRICTION * dt);
            else if (ball.speed < 0.0f)
                ball.speed = std::min(0.0f, ball.speed + BALL_FRICTION * dt);
        }

        // 4. Atualização do salto com curva de Bézier cúbica
        if (ball.is_jumping)
        {
            ball.jump_timer += dt;
            float t = ball.jump_timer / JUMP_DURATION;

            if (t >= 1.0f)
            {
                ball.is_jumping     = false;
                ball.jump_timer     = 0.0f;
                ball.jump_elevation = 0.0f;

                // Ao pousar, verificamos se existe bloco sob a bola
                if (HasBlockAt(ball.current_block))
                {
                    ball.landing_timer = LAND_SQUISH_TIME;
                }
                else
                {
                    // Verifica se a borda da bola ainda toca o bloco anterior ou próximo
                    glm::vec4 right = crossproduct(ball.forward, ball.up);
                    float sf = dotproduct(ball.surface_offset, ball.forward);
                    GridCoord neighbor_f = OffsetGrid(ball.current_block, ball.forward);
                    GridCoord neighbor_b = OffsetGrid(ball.current_block, -ball.forward);

                    if (sf > BLOCK_HALF - BALL_RADIUS && HasBlockAt(neighbor_f))
                    {
                        ball.current_block  = neighbor_f;
                        ball.surface_offset = (sf - BLOCK_SIZE) * ball.forward
                                            + dotproduct(ball.surface_offset, right) * right;
                        ball.landing_timer  = LAND_SQUISH_TIME;
                    }
                    else if (sf < -BLOCK_HALF + BALL_RADIUS && HasBlockAt(neighbor_b))
                    {
                        ball.current_block  = neighbor_b;
                        ball.surface_offset = (sf + BLOCK_SIZE) * ball.forward
                                            + dotproduct(ball.surface_offset, right) * right;
                        ball.landing_timer  = LAND_SQUISH_TIME;
                    }
                    else
                    {
                        ball.is_falling    = true;
                        ball.fall_speed    = 0.0f;
                        ball.fall_distance = 0.0f;
                    }
                }
            }
            else
            {
                // Pontos de controle 3D da curva de Bézier cúbica na direção da normal up:
                // P0 = 0, P1 = (4/3)*H_max*up, P2 = (4/3)*H_max*up, P3 = 0
                // Em t = 0.5, B(0.5) = (3/4)*(4/3)*H_max*up = H_max*up
                float ctrl_h = (4.0f / 3.0f) * JUMP_MAX_HEIGHT;
                glm::vec4 p0 = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
                glm::vec4 p1 = ctrl_h * ball.up;
                glm::vec4 p2 = ctrl_h * ball.up;
                glm::vec4 p3 = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

                glm::vec4 bezier_offset = EvaluateCubicBezier(p0, p1, p2, p3, t);
                ball.jump_elevation     = dotproduct(bezier_offset, ball.up);
            }
        }

        // 5. Deslocamento tangencial e transições de superfície/gravidade
        float ds = ball.speed * dt;
        if (std::fabs(ds) > 1e-6f)
        {
            int move_sign      = (ds >= 0.0f) ? 1 : -1;
            glm::vec4 move_dir = (move_sign > 0) ? ball.forward : -ball.forward;
            float abs_ds       = std::fabs(ds);

            // Alinhamento suave ao centro da trilha (eixo lateral right -> 0) enquanto rola
            glm::vec4 right  = crossproduct(ball.forward, ball.up);
            float s_move     = dotproduct(ball.surface_offset, move_dir);
            float s_lat      = dotproduct(ball.surface_offset, right);
            float lat_decay  = std::exp(-8.0f * dt);
            float new_s_lat  = s_lat * lat_decay;

            s_move += abs_ds;
            ball.surface_offset = s_move * move_dir + new_s_lat * right;

            // Rolamento físico sem deslizar: Delta theta = Delta S / R
            AccumulateRollingRotation(ball, ball.up, move_dir, abs_ds);

            // Verificação dos 3 casos de transição de bloco:
            GridCoord wall_block = OffsetGrid(ball.current_block, move_dir + ball.up);
            GridCoord next_block = OffsetGrid(ball.current_block, move_dir);

            // CASO A: Parede interna à frente (canto côncavo de 90°)
            if (HasBlockAt(wall_block) && s_move >= (BLOCK_HALF - BALL_RADIUS))
            {
                // Se estiver no ar perto do topo da parede e não houver bloco acima dela, pousa no topo da parede
                GridCoord above_wall = OffsetGrid(wall_block, ball.up);
                if (ball.is_jumping && ball.jump_elevation > 0.95f * BLOCK_SIZE && !HasBlockAt(above_wall))
                {
                    if (s_move > BLOCK_HALF)
                    {
                        ball.current_block  = wall_block;
                        ball.surface_offset = (s_move - BLOCK_SIZE) * move_dir + new_s_lat * right;
                        ball.is_jumping     = false;
                        ball.jump_timer     = 0.0f;
                        ball.jump_elevation = 0.0f;
                        ball.landing_timer  = LAND_SQUISH_TIME;
                    }
                }
                else
                {
                    // Transição côncava: a bola adere à parede e reorienta a gravidade para -new_up
                    float excess        = s_move - (BLOCK_HALF - BALL_RADIUS);
                    glm::vec4 new_up    = SnapToAxis(-move_dir);
                    glm::vec4 new_m_dir = SnapToAxis(ball.up);

                    // Se estava saltando, projeta a elevação na nova coordenada ao longo da parede
                    float start_coord   = -(BLOCK_HALF - BALL_RADIUS) + excess + ball.jump_elevation;
                    ball.is_jumping     = false;
                    ball.jump_timer     = 0.0f;
                    ball.jump_elevation = 0.0f;

                    ball.current_block  = wall_block;
                    ball.up             = new_up;
                    ball.forward        = (move_sign > 0) ? new_m_dir : -new_m_dir;
                    ball.surface_offset = start_coord * new_m_dir + new_s_lat * right;
                }
            }
            // CASO B: Passou da borda do bloco atual (s_move > BLOCK_HALF)
            else if (s_move > BLOCK_HALF)
            {
                if (HasBlockAt(next_block) || ball.is_jumping)
                {
                    // Continuação plana para o próximo bloco (ou voando sobre um vão durante o salto)
                    ball.current_block  = next_block;
                    ball.surface_offset = (s_move - BLOCK_SIZE) * move_dir + new_s_lat * right;
                }
                else
                {
                    // CASO C: Borda externa convexa! A bola dobra 90 graus para a face adjacente do mesmo bloco
                    float excess          = s_move - BLOCK_HALF;
                    ball.is_wrapping_edge = true;
                    ball.wrap_angle       = excess / BALL_RADIUS;
                    ball.wrap_start_up    = ball.up;
                    ball.wrap_move_dir    = move_dir;
                    ball.wrap_move_sign   = move_sign;
                    ball.wrap_pivot       = BlockCenterWorld(ball.current_block)
                                          + BLOCK_HALF * ball.up
                                          + BLOCK_HALF * move_dir
                                          + new_s_lat * right;
                }
            }
        }

        // Atualiza posição global do centro da bola
        if (!ball.is_wrapping_edge && !ball.is_falling)
        {
            ball.position_world = BlockCenterWorld(ball.current_block)
                                + (BLOCK_HALF + BALL_RADIUS + ball.jump_elevation) * ball.up
                                + ball.surface_offset;
        }
        else if (ball.is_wrapping_edge)
        {
            glm::vec4 u_inst = std::cos(ball.wrap_angle) * ball.wrap_start_up
                             + std::sin(ball.wrap_angle) * ball.wrap_move_dir;
            ball.position_world = ball.wrap_pivot + BALL_RADIUS * u_inst;
        }
    }

    // 6. Atualiza a animação de deformação (squish)
    UpdateBallSquish(ball, dt);

    // 7. Suaviza os vetores de orientação da câmera para acompanhar trocas de superfície e giros de 90°
    glm::vec4 target_up = ball.is_wrapping_edge
        ? (std::cos(ball.wrap_angle) * ball.wrap_start_up + std::sin(ball.wrap_angle) * ball.wrap_move_dir)
        : ball.up;
    glm::vec4 target_forward = ball.is_wrapping_edge
        ? (ball.wrap_move_sign >= 0
            ? (std::cos(ball.wrap_angle) * ball.wrap_move_dir - std::sin(ball.wrap_angle) * ball.wrap_start_up)
            : (-std::cos(ball.wrap_angle) * ball.wrap_move_dir + std::sin(ball.wrap_angle) * ball.wrap_start_up))
        : ball.forward;

    float cam_smooth = 1.0f - std::exp(-10.0f * dt);
    if (dotproduct(ball.cam_up, target_up) < -0.95f)
        ball.cam_up = ball.cam_up + 0.2f * target_forward;
    ball.cam_up = ball.cam_up + cam_smooth * (target_up - ball.cam_up);
    if (norm(ball.cam_up) < 1e-4f)
        ball.cam_up = target_up;
    ball.cam_up = ball.cam_up / norm(ball.cam_up);

    // Aplica o quadrante orbital (0, 90, 180, 270 graus em torno da normal)
    float orbit_angle = (float)ball.cam_orbit_quadrant * (3.14159265f / 2.0f);
    glm::vec4 orbit_forward = Matrix_Rotate(orbit_angle, target_up) * target_forward;

    if (dotproduct(ball.cam_forward, orbit_forward) < -0.95f)
        ball.cam_forward = ball.cam_forward + 0.2f * crossproduct(orbit_forward, ball.cam_up);
    ball.cam_forward = ball.cam_forward + cam_smooth * (orbit_forward - ball.cam_forward);
    // Ortonormaliza cam_forward em relação a cam_up (Gram-Schmidt)
    ball.cam_forward = ball.cam_forward - dotproduct(ball.cam_forward, ball.cam_up) * ball.cam_up;
    if (norm(ball.cam_forward) < 1e-4f)
        ball.cam_forward = orbit_forward;
    ball.cam_forward = ball.cam_forward / norm(ball.cam_forward);
}

#endif // _BALL_MOVEMENT_H
