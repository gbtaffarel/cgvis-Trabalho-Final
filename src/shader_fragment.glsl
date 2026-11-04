#version 330 core

// Atributos de fragmentos recebidos como entrada ("in") pelo Fragment Shader.
// Neste exemplo, este atributo foi gerado pelo rasterizador como a
// interpolação da posição global e a normal de cada vértice, definidas em
// "shader_vertex.glsl" e "main.cpp".
in vec4 position_world;
in vec4 normal;

// Posição do vértice atual no sistema de coordenadas local do modelo.
in vec4 position_model;

// Coordenadas de textura obtidas do arquivo OBJ (se existirem!)
in vec2 texcoords;

// Matrizes computadas no código C++ e enviadas para a GPU
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// Identificador que define qual objeto está sendo desenhado no momento
#define SPHERE 0
#define BUNNY  1
#define PLANE  2
uniform int object_id;

// Parâmetros da axis-aligned bounding box (AABB) do modelo
uniform vec4 bbox_min;
uniform vec4 bbox_max;

// Variáveis para acesso das imagens de textura
uniform sampler2D TextureImage0;
uniform sampler2D TextureImage1;
uniform sampler2D TextureImage2;

// O valor de saída ("out") de um Fragment Shader é a cor final do fragmento.
out vec4 color;

// Constantes
#define M_PI   3.14159265358979323846
#define M_PI_2 1.57079632679489661923

void main()
{
    // Obtemos a posição da câmera utilizando a inversa da matriz que define o
    // sistema de coordenadas da câmera.
    vec4 origin = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 camera_position = inverse(view) * origin;

    // O fragmento atual é coberto por um ponto que percente à superfície de um
    // dos objetos virtuais da cena. Este ponto, p, possui uma posição no
    // sistema de coordenadas global (World coordinates). Esta posição é obtida
    // através da interpolação, feita pelo rasterizador, da posição de cada
    // vértice.
    vec4 p = position_world;

    // Normal do fragmento atual, interpolada pelo rasterizador a partir das
    // normais de cada vértice.
    vec4 n = normalize(normal);

    // Vetor que define o sentido da fonte de luz principal e secundária em relação ao ponto atual.
    vec4 l  = normalize(vec4(0.65, 1.0, 0.55, 0.0));
    vec4 l2 = normalize(vec4(-0.6, -0.7, -0.5, 0.0));

    // Vetor que define o sentido da câmera em relação ao ponto atual.
    vec4 v = normalize(camera_position - p);

    // Vetor de reflexão de Phong r = -l + 2*(n.l)*n
    vec4 r = -l + 2.0 * n * dot(n, l);

    // Coordenadas de textura U e V
    float U = 0.0;
    float V = 0.0;

    // Coeficientes de refletância difusa e especular
    vec3  Kd0 = vec3(0.0);
    vec3  Ks  = vec3(0.0);
    float q   = 16.0;

    if ( object_id == SPHERE )
    {
        // Projeção esférica em coordenadas do modelo (acompanha a rotação da bola!)
        vec4 bbox_center = (bbox_min + bbox_max) / 2.0;
        vec4 d = position_model - bbox_center;

        float rho   = length(d);
        float theta = atan(d.x, d.z);
        float phi   = asin(clamp(d.y / rho, -1.0, 1.0));

        U = (theta + M_PI) / (2.0 * M_PI);
        V = (phi + M_PI_2) / M_PI;

        // Textura base modulada com os 6 gomos coloridos da bola de praia do Kula World
        vec3 tex_sample = texture(TextureImage0, vec2(U, V)).rgb;
        float tex_detail = mix(0.82, 1.05, dot(tex_sample, vec3(0.333)));

        int sector = int(floor(U * 6.0)) % 6;
        vec3 stripe_color = vec3(0.95, 0.95, 0.95);
        if (sector == 0)      stripe_color = vec3(0.96, 0.80, 0.08); // Amarelo
        else if (sector == 1) stripe_color = vec3(0.95, 0.95, 0.96); // Branco
        else if (sector == 2) stripe_color = vec3(0.88, 0.14, 0.10); // Vermelho
        else if (sector == 3) stripe_color = vec3(0.95, 0.95, 0.96); // Branco
        else if (sector == 4) stripe_color = vec3(0.12, 0.72, 0.22); // Verde
        else                  stripe_color = vec3(0.10, 0.35, 0.90); // Azul

        // Calotas polares brancas como na bola de praia clássica
        if (V < 0.12 || V > 0.88)
            stripe_color = vec3(0.96, 0.96, 0.96);

        Kd0 = stripe_color * tex_detail;
        // Aspecto plástico brilhante para a bola (especular forte e concentrado)
        Ks  = vec3(0.65, 0.65, 0.65);
        q   = 48.0;
    }
    else if ( object_id == BUNNY )
    {
        float minx = bbox_min.x;
        float maxx = bbox_max.x;

        float miny = bbox_min.y;
        float maxy = bbox_max.y;

        U = (position_model.x - minx) / (maxx - minx);
        V = (position_model.y - miny) / (maxy - miny);

        Kd0 = texture(TextureImage0, vec2(U,V)).rgb;
        Ks  = vec3(0.25, 0.25, 0.25);
        q   = 24.0;
    }
    else if ( object_id == PLANE )
    {
        // Coordenadas de textura do bloco/plano, obtidas do VAO/OBJ.
        U = texcoords.x;
        V = texcoords.y;

        vec3 stone_tex = texture(TextureImage1, vec2(U, V)).rgb;
        // Tom arenito egípcio inspirado nas plataformas do Kula World + borda sutil entre blocos
        float border_dist = min(min(U, 1.0 - U), min(V, 1.0 - V));
        float bevel = smoothstep(0.0, 0.05, border_dist);
        Kd0 = stone_tex * vec3(1.15, 0.88, 0.60) * mix(0.68, 1.0, bevel);
        Ks  = vec3(0.08, 0.07, 0.05);
        q   = 12.0;
    }

    // Equação de Iluminação de Phong com luz ambiente e luz de preenchimento (para faces inferiores/paredes)
    float lambert1 = max(0.0, dot(n, l));
    float lambert2 = 0.35 * max(0.0, dot(n, l2));
    float phong    = pow(max(0.0, dot(r, v)), q);
    vec3  Ia       = vec3(0.22, 0.22, 0.24);

    color.rgb = Kd0 * (lambert1 + lambert2 + Ia) + Ks * phong;

    // NOTE: Se você quiser fazer o rendering de objetos transparentes, é
    // necessário:
    // 1) Habilitar a operação de "blending" de OpenGL logo antes de realizar o
    //    desenho dos objetos transparentes, com os comandos abaixo no código C++:
    //      glEnable(GL_BLEND);
    //      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // 2) Realizar o desenho de todos objetos transparentes *após* ter desenhado
    //    todos os objetos opacos; e
    // 3) Realizar o desenho de objetos transparentes ordenados de acordo com
    //    suas distâncias para a câmera (desenhando primeiro objetos
    //    transparentes que estão mais longe da câmera).
    // Alpha default = 1 = 100% opaco = 0% transparente
    color.a = 1;

    // Cor final com correção gamma, considerando monitor sRGB.
    // Veja https://en.wikipedia.org/w/index.php?title=Gamma_correction&oldid=751281772#Windows.2C_Mac.2C_sRGB_and_TV.2Fvideo_standard_gammas
    color.rgb = pow(color.rgb, vec3(1.0,1.0,1.0)/2.2);
} 

