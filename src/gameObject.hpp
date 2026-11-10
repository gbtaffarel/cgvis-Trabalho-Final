// Headers da biblioteca GLM: criação de matrizes e vetores.
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

#ifndef GAMEOBJECT_HPP
#define GAMEOBJECT_HPP

struct transform{
    glm::vec3 position;
    glm::vec3 rotation;
};

struct gameObject{
    int id;
    char* name;
    transform transform;
    int objIndex; //Indice utlizado na definição de qual objeto será desenhado
};


#endif