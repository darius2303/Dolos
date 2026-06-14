#include <stdio.h>
#define MAX 100

int stack[MAX];
int top = -1;

// Push an element onto the stack
void push(int value) {
    if (top == MAX - 1) {
        printf("\nStack Overflow");
    } else {
        stack[++top] = value;
    }
}

// Pop an element from the stack
int pop() {
    if (top == -1) {
        printf("\nStack Underflow");
        return -1;
    } else {
        return stack[top--];
    }
}

// Peek at the top element of the stack
void peek() {
    if (top == -1) {
        printf("\nStack is Empty");
    } else {
        printf("\nTop Element is: %d", stack[top]);
    }
}

// Display all elements in the stack
void display() {
    if (top == -1) {
        printf("\nStack is Empty");
    } else {
        printf("\nStack Elements:\n");
        for (int i = 0; i <= top; i++) {
            printf("%d ", stack[i]);
        }
        printf("\n");
    }
}

int main() {
    int n, value, choice, popped_value;

    // Initial input for stack elements
    printf("Enter the number of elements to add: ");
    scanf("%d", &n);

    for (int i = 0; i < n; i++) {
        printf("Enter Value-%d: ", i + 1);
        scanf("%d", &value);
        push(value);
    }

    // Menu-driven program for stack operations
    while (1) {
        printf("\n----------------------");
        printf("\n\nStack Operations:\n");
        printf("1. Push\n");
        printf("2. Pop\n");
        printf("3. Peek\n");
        printf("4. Display\n");
        printf("5. Exit\n");
        printf("Enter your choice: ");
        scanf("%d", &choice);
        printf("\n----------------------");

        switch (choice) {
            case 1:
                printf("\nEnter Value: ");
                scanf("%d", &value);
                push(value);
                break;
            case 2:
                popped_value = pop();
                if (popped_value != -1) {
                    printf("\nPopped: %d", popped_value);
                }
                break;
            case 3:
                peek();
                break;
            case 4:
                display();
                break;
            case 5:
                printf("\nExiting program...");
                return 0;
            default:
                printf("\nEnter a Valid Choice...");
        }
    }
}
