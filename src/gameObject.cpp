#include "gameObject.hpp"

gameObject BuildGameObject(int id, char * name, int objIndex, int &totalSceneObjects){
    gameObject obj;

    obj.id = id;
    obj.name = name;
    transform t;
    t.position = glm::vec3(0.0f,0.0f,0.0f);
    t.rotation = glm::vec3(0.0f,0.0f,0.0f);
    obj.objIndex = objIndex;
    obj.active = true;

    totalSceneObjects++;
    return obj;
}

void SetObjectPosition(gameObject &obj, glm::vec3 pos){
    obj.transform.position = pos;
}

void SetObjectRotation(gameObject &obj, glm::vec3 rot){
    obj.transform.rotation = rot;
}

