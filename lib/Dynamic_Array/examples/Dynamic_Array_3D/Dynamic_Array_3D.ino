//Implementacion de un array dinamico 3D en arduino
//Incluimos la libreria desarrollada
#include<Dynamic_Array.h>
//Creamos el constructor, este no recibe ningun parametro
Dynamic_Array W=Dynamic_Array();
void setup() {
   Serial.begin(9600);
   //Cosultamos la capacidad de memoria SRAM Disponible en bytes
    int cap=W.freeRam();
    //Imprimimos la capacidad de memoria SRAM Disponible en bytes
    Serial.print("Capacidad de memoria SRAM Disponible: ");
    Serial.println(cap);
    //Creamos un nuevo arreglo tridimensional 3 filas,4 columnas, 5 en profundidad
    W.NewArray_3D(1,1,1800);
    //Cosultamos nuevamente la capacidad de memoria disponble
    //para calcular la memoria consumida por el arreglo tridimensional creado
    int cap_array=cap-W.freeRam();
    //Imprimimos capacidad de memoria consumida por el array
    Serial.print("Capacidad de memoria consumida por el array: ");
    Serial.println(cap_array);
    // Escribimos en el array creado

   for (double k = 0; k < W.GetDepth(); k++)
   {
     for (double i = 0; i < W.GetColumns(); i++)
        {
          for (double j = 0; j < W.GetRows(); j++)
            {
              W.WriteArray_3D(j,i,k,5);
            }
         }
   }
   //Leemos e imprimimos el dato guardado en
   //fila 1 columna 1 profundidad 1 del array
    Serial.print("Dato guardado en fila 0 columna 0 profundidad 0 del array:  ");
    Serial.println(W.ReadArray_3D(0,0,0));
   //Imprimimos el array por puerto serial
    Serial.println("Datos del array:  ");
    //W.PrintArray_3D();
    //Leemos e imprimimos el numero de filas del array
    Serial.print("Numero de filas del array:  ");
    Serial.println(W.GetRows());
    //Leemos e imprimimos el numero de columnas del array
    Serial.print("Numero de columnas del array:  ");
    Serial.println(W.GetColumns());
    //Leemos e imprimimos la profundidad del array 
    //(para array de 3D)
    Serial.print("Profundidad del array:  ");
    Serial.println(W.GetDepth());
    //eliminamos el arrray para liberar memoria
    W.DeleteArray_3D();
    //Cosultamos e imprimimos la capacidad de memoria SRAM Disponible en bytes
    //Para verificar que la memoria se libero correctamente
    Serial.print("Memoria SRAM Disponible despues de eliminar el array :");
    Serial.println(W.freeRam());
  }

void loop() {
  

}
