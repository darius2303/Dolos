#include <stdio.h>
#include <stdbool.h>

#define MAXIMUM_SIZE 1000

typedef struct {
    int array[MAXIMUM_SIZE];  
    int head;        
} Stack;

void init(Stack *stack) {
    stack->head = -1;  
}

bool isEmpty(Stack *stack) {
    return stack->head == -1;  
}

bool isFull(Stack *stack) {
    return stack->head >= MAXIMUM_SIZE - 1;  
}

void push(Stack *stack, int value) {
    if (isFull(stack)) {
        printf("Stack Overflow\n");
        return;
    }
    stack->array[++stack->head] = value;
    printf("Pushed %d onto the stack\n", value);
}

int pop(Stack *stack) {
    if (isEmpty(stack)) {
        printf("Stack Underflow\n");
        return -1;
    }

    int popped = stack->array[stack->head];
    stack->head--;
    printf("Popped %d from the stack\n", popped);
    return popped;
}

int peek(Stack *stack) {
    if (isEmpty(stack)) {
        printf("Stack is empty\n");
        return -1;
    }
    return stack->array[stack->head];
}

int main() {
    Stack stack;
    init(&stack);  

    push(&stack, 3);
    printf("Top element: %d\n", peek(&stack));

    push(&stack, 5);
    printf("Top element: %d\n", peek(&stack));

    push(&stack, 2);
    printf("Top element: %d\n", peek(&stack));

    push(&stack, 8);
    printf("Top element: %d\n", peek(&stack));

    while (!isEmpty(&stack)) {
        printf("Top element: %d\n", peek(&stack));
        printf("Popped element: %d\n", pop(&stack));
    }

    return 0;
}