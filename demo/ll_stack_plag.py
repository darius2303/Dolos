class Node:
    def __init__(self, val):
        self.val = val
        self.nxt = None

class Stack:
    def __init__(self):
        self.top = None
        self.len = 0

    def push(self, val):
        newnode = Node(val)
        if self.top:
            newnode.nxt = self.top
        self.top = newnode
        self.len += 1

    def pop(self):
        if self.isEmpty():
            return "Stack is empty"
        popped = self.top
        self.top = self.top.nxt
        self.len -= 1
        return popped.val

    def peek(self):
        if self.isEmpty():
            return "Stack is empty"
        return self.top.val

    def isEmpty(self):
        return self.len == 0

    def stackSize(self):
        return self.len

    def traverseAndPrint(self):
        curr = self.top
        while curr:
            print(curr.val, end=" -> ")
            curr = curr.nxt
        print()

myStack = Stack()
myStack.push('A')
myStack.push('B')
myStack.push('C')
print("LinkedList: ", end="")
myStack.traverseAndPrint() 
print("Peek: ", myStack.peek())
print("Pop: ", myStack.pop())
print("LinkedList after Pop: ", end="")
myStack.traverseAndPrint() 
print("isEmpty: ", myStack.isEmpty())
print("Size: ", myStack.stackSize())