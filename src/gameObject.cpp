#include "gameObject.hpp"

gameObject BuildGameObject(int id, char * name, int objIndex){
    gameObject obj;

    obj.id = id;
    obj.name = name;
    transform t;
    t.position = glm::vec3(0.0f,0.0f,0.0f);
    t.rotation = glm::vec3(0.0f,0.0f,0.0f);
    obj.objIndex = objIndex;

    return obj;
}