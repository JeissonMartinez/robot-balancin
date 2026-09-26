//Implementacion de un arrat dinamico 2D en arduino
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
   //Creamos un nuevo arreglo bidimensional 6 filas,5columnas
    W.NewArray_2D(10,10);
   //Cosultamos nuevamente la capacidad de memoria disponble
   //para calcular la memoria consumida por el arreglo bidimensional creado
    int cap_array=cap-W.freeRam();
    //Imprimimos capacidad de memoria consumida por el array
   Serial.print("Capacidad de memoria consumida por el array: ");
   Serial.println(cap_array);
   // Escribimos en el array creado
   for (double i = 0; i < W.GetColumns(); i++)
   {for (double j = 0; j < W.GetRows(); j++)
     { W.WriteArray_2D(j,i,i*i);
    }
    }
    //Leemos e imprimimos el dato guardado en fila 1 columna 1 del array
    Serial.print("Dato guardado en fila 1 columna 1 del array:  ");
    Serial.println(W.ReadArray_2D(1,1));
    //Imprimimos el array por puerto serial
    Serial.println("Datos del array:  ");
     W.PrintArray_2D();
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
    W.DeleteArray_2D();
    //Cosultamos e imprimimos la capacidad de memoria SRAM Disponible en bytes
    //Para verificar que la memoria se libero correctamente
    Serial.print("Memoria SRAM Disponible despues de eliminar el array :");
    Serial.println(W.freeRam());

 
  }

void loop() {
  

}
